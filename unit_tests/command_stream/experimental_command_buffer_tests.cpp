/*
 * Copyright (c) 2018, Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "runtime/memory_manager/memory_constants.h"
#include "runtime/memory_manager/memory_manager.h"
#include "unit_tests/fixtures/ult_command_stream_receiver_fixture.h"
#include "unit_tests/helpers/debug_manager_state_restore.h"
#include "unit_tests/mocks/mock_experimental_command_buffer.h"
#include "test.h"
#include "gtest/gtest.h"

using namespace OCLRT;

struct ExperimentalCommandBufferTest : public UltCommandStreamReceiverTest {
    void SetUp() override {
        dbgRestore.reset(new DebugManagerStateRestore());
        DebugManager.flags.EnableExperimentalCommandBuffer.set(1);
        UltCommandStreamReceiverTest::SetUp();
    }

    std::unique_ptr<DebugManagerStateRestore> dbgRestore;
};

struct MockExperimentalCommandBufferTest : public UltCommandStreamReceiverTest {
    void SetUp() override {
        UltCommandStreamReceiverTest::SetUp();
        pDevice->getCommandStreamReceiver().setExperimentalCmdBuffer(
            std::unique_ptr<ExperimentalCommandBuffer>(new MockExperimentalCommandBuffer(&pDevice->getCommandStreamReceiver())));
    }
};

HWTEST_F(MockExperimentalCommandBufferTest, givenEnabledExperimentalCmdBufferWhenCsrIsFlushedThenExpectProperlyFilledExperimentalCmdBuffer) {
    using MI_BATCH_BUFFER_START = typename FamilyType::MI_BATCH_BUFFER_START;
    using MI_BATCH_BUFFER_END = typename FamilyType::MI_BATCH_BUFFER_END;
    using PIPE_CONTROL = typename FamilyType::PIPE_CONTROL;
    using MI_SEMAPHORE_WAIT = typename FamilyType::MI_SEMAPHORE_WAIT;

    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();
    commandStreamReceiver.storeMakeResidentAllocations = true;
    MockExperimentalCommandBuffer *mockExCmdBuffer = static_cast<MockExperimentalCommandBuffer *>(commandStreamReceiver.experimentalCmdBuffer.get());

    flushTask(commandStreamReceiver);

    ASSERT_NE(nullptr, mockExCmdBuffer->currentStream.get());
    ASSERT_NE(nullptr, mockExCmdBuffer->currentStream->getGraphicsAllocation());
    uint64_t exCmdBufferGpuAddr = mockExCmdBuffer->currentStream->getGraphicsAllocation()->getGpuAddress();
    EXPECT_TRUE(commandStreamReceiver.isMadeResident(mockExCmdBuffer->currentStream->getGraphicsAllocation()));

    ASSERT_NE(nullptr, mockExCmdBuffer->experimentalAllocation);
    uint64_t exAllocationGpuAddr = mockExCmdBuffer->experimentalAllocation->getGpuAddress();
    EXPECT_TRUE(commandStreamReceiver.isMadeResident(mockExCmdBuffer->experimentalAllocation));

    ASSERT_NE(nullptr, mockExCmdBuffer->timestamps);
    EXPECT_TRUE(commandStreamReceiver.isMadeResident(mockExCmdBuffer->timestamps));

    constexpr uint32_t expectedTsOffset = 2 * sizeof(uint64_t);
    EXPECT_EQ(expectedTsOffset, mockExCmdBuffer->timestampsOffset);
    constexpr uint32_t expectedExOffset = 0;
    EXPECT_EQ(expectedExOffset, mockExCmdBuffer->experimentalAllocationOffset);

    constexpr uint32_t expectedSemaphoreVal = 1;
    uintptr_t actualSemaphoreAddr = reinterpret_cast<uintptr_t>(mockExCmdBuffer->experimentalAllocation->getUnderlyingBuffer()) + mockExCmdBuffer->experimentalAllocationOffset;
    uint32_t *actualSemaphoreVal = reinterpret_cast<uint32_t *>(actualSemaphoreAddr);
    EXPECT_EQ(expectedSemaphoreVal, *actualSemaphoreVal);

    HardwareParse hwParserCsr;
    hwParserCsr.parseCommands<FamilyType>(commandStreamReceiver.commandStream, 0);
    GenCmdList bbList = hwParserCsr.getCommandsList<MI_BATCH_BUFFER_START>();
    MI_BATCH_BUFFER_START *bbStart = nullptr;
    GenCmdList::iterator it = bbList.begin();
    ASSERT_NE(bbList.end(), it);
    bbStart = reinterpret_cast<MI_BATCH_BUFFER_START *>(*it);
    ASSERT_NE(nullptr, bbStart);
    EXPECT_EQ(exCmdBufferGpuAddr, bbStart->getBatchBufferStartAddressGraphicsaddress472());
    EXPECT_EQ(MI_BATCH_BUFFER_START::SECOND_LEVEL_BATCH_BUFFER_SECOND_LEVEL_BATCH, bbStart->getSecondLevelBatchBuffer());

    MI_BATCH_BUFFER_END *bbEnd = nullptr;
    PIPE_CONTROL *pipeControl = nullptr;
    MI_SEMAPHORE_WAIT *semaphoreCmd = nullptr;

    HardwareParse hwParserExCmdBuffer;
    hwParserExCmdBuffer.parseCommands<FamilyType>(*mockExCmdBuffer->currentStream, 0);
    it = hwParserExCmdBuffer.cmdList.begin();
    GenCmdList::iterator end = hwParserExCmdBuffer.cmdList.end();
    //1st PIPE_CONTROL with CS Stall
    ASSERT_NE(end, it);
    pipeControl = genCmdCast<PIPE_CONTROL *>(*it);
    ASSERT_NE(nullptr, pipeControl);
    EXPECT_EQ(1u, pipeControl->getCommandStreamerStallEnable());

    //2nd PIPE_CONTROL with ts addr
    uint64_t timeStampAddress = mockExCmdBuffer->timestamps->getGpuAddress();
    uint32_t expectedTsAddress = static_cast<uint32_t>(timeStampAddress & 0x0000FFFFFFFFULL);
    uint32_t expectedTsAddressHigh = static_cast<uint32_t>(timeStampAddress >> 32);
    it++;
    ASSERT_NE(end, it);
    pipeControl = genCmdCast<PIPE_CONTROL *>(*it);
    ASSERT_NE(nullptr, pipeControl);
    EXPECT_EQ(1u, pipeControl->getCommandStreamerStallEnable());
    EXPECT_EQ(PIPE_CONTROL::POST_SYNC_OPERATION_WRITE_TIMESTAMP, pipeControl->getPostSyncOperation());
    EXPECT_EQ(expectedTsAddress, pipeControl->getAddress());
    EXPECT_EQ(expectedTsAddressHigh, pipeControl->getAddressHigh());

    //MI_SEMAPHORE_WAIT
    it++;
    ASSERT_NE(end, it);
    semaphoreCmd = genCmdCast<MI_SEMAPHORE_WAIT *>(*it);
    ASSERT_NE(nullptr, semaphoreCmd);
    EXPECT_EQ(expectedSemaphoreVal, semaphoreCmd->getSemaphoreDataDword());
    EXPECT_EQ(exAllocationGpuAddr, semaphoreCmd->getSemaphoreGraphicsAddress());
    EXPECT_EQ(MI_SEMAPHORE_WAIT::COMPARE_OPERATION_SAD_EQUAL_SDD, semaphoreCmd->getCompareOperation());

    //3rd PIPE_CONTROL with CS stall
    it++;
    ASSERT_NE(end, it);
    pipeControl = genCmdCast<PIPE_CONTROL *>(*it);
    ASSERT_NE(nullptr, pipeControl);
    EXPECT_EQ(1u, pipeControl->getCommandStreamerStallEnable());

    //4th PIPE_CONTROL with ts addr
    timeStampAddress = mockExCmdBuffer->timestamps->getGpuAddress() + sizeof(uint64_t);
    expectedTsAddress = static_cast<uint32_t>(timeStampAddress & 0x0000FFFFFFFFULL);
    expectedTsAddressHigh = static_cast<uint32_t>(timeStampAddress >> 32);
    it++;
    ASSERT_NE(end, it);
    pipeControl = genCmdCast<PIPE_CONTROL *>(*it);
    ASSERT_NE(nullptr, pipeControl);
    EXPECT_EQ(1u, pipeControl->getCommandStreamerStallEnable());
    EXPECT_EQ(PIPE_CONTROL::POST_SYNC_OPERATION_WRITE_TIMESTAMP, pipeControl->getPostSyncOperation());
    EXPECT_EQ(expectedTsAddress, pipeControl->getAddress());
    EXPECT_EQ(expectedTsAddressHigh, pipeControl->getAddressHigh());

    //BB_END
    it++;
    ASSERT_NE(end, it);
    bbEnd = genCmdCast<MI_BATCH_BUFFER_END *>(*it);
    ASSERT_NE(nullptr, bbEnd);
}

HWTEST_F(MockExperimentalCommandBufferTest, givenEnabledExperimentalCmdBufferWhenCsrIsNotFlushedThenExperimentalBufferLinearStreamIsNotCreatedAndCmdBufferCommandsHaveProperlyOffsetedAddresses) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();
    commandStreamReceiver.storeMakeResidentAllocations = true;
    MockExperimentalCommandBuffer *mockExCmdBuffer = static_cast<MockExperimentalCommandBuffer *>(commandStreamReceiver.experimentalCmdBuffer.get());

    EXPECT_EQ(nullptr, mockExCmdBuffer->currentStream.get());
    EXPECT_NE(nullptr, mockExCmdBuffer->experimentalAllocation);
    EXPECT_FALSE(commandStreamReceiver.isMadeResident(mockExCmdBuffer->experimentalAllocation));

    EXPECT_NE(nullptr, mockExCmdBuffer->timestamps);
    EXPECT_FALSE(commandStreamReceiver.isMadeResident(mockExCmdBuffer->timestamps));

    constexpr uint32_t expectedTsOffset = 0;
    EXPECT_EQ(expectedTsOffset, mockExCmdBuffer->timestampsOffset);

    constexpr uint32_t expectedExOffset = 0;
    EXPECT_EQ(expectedExOffset, mockExCmdBuffer->experimentalAllocationOffset);
}

HWTEST_F(MockExperimentalCommandBufferTest, givenEnabledExperimentalCmdBufferWhenMemoryManagerIsNotAvailableThenExperimentalBufferAllocationsRemainAllocated) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();
    MockExperimentalCommandBuffer *mockExCmdBuffer = static_cast<MockExperimentalCommandBuffer *>(commandStreamReceiver.experimentalCmdBuffer.get());

    EXPECT_NE(nullptr, mockExCmdBuffer->experimentalAllocation);
    EXPECT_NE(nullptr, mockExCmdBuffer->timestamps);

    auto experimentalAllocation = mockExCmdBuffer->experimentalAllocation;
    auto timestamps = mockExCmdBuffer->timestamps;
    auto memoryManager = commandStreamReceiver.getMemoryManager();

    //null memManager
    commandStreamReceiver.setMemoryManager(nullptr);
    //delete experimental cmd buffer and verify its allocations remain intact
    commandStreamReceiver.setExperimentalCmdBuffer(std::move(std::unique_ptr<ExperimentalCommandBuffer>(nullptr)));
    memoryManager->freeGraphicsMemory(experimentalAllocation);
    memoryManager->freeGraphicsMemory(timestamps);
    commandStreamReceiver.setMemoryManager(memoryManager);
}

HWTEST_F(MockExperimentalCommandBufferTest, givenEnabledExperimentalCmdBufferWhenCsrIsFlushedTwiceThenExpectProperlyFilledExperimentalCmdBufferAndTimestampOffset) {
    using MI_BATCH_BUFFER_START = typename FamilyType::MI_BATCH_BUFFER_START;
    using MI_BATCH_BUFFER_END = typename FamilyType::MI_BATCH_BUFFER_END;
    using PIPE_CONTROL = typename FamilyType::PIPE_CONTROL;

    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();
    commandStreamReceiver.storeMakeResidentAllocations = true;
    MockExperimentalCommandBuffer *mockExCmdBuffer = static_cast<MockExperimentalCommandBuffer *>(commandStreamReceiver.experimentalCmdBuffer.get());

    flushTask(commandStreamReceiver);
    size_t csrCmdBufferOffset = commandStreamReceiver.commandStream.getUsed();

    ASSERT_NE(nullptr, mockExCmdBuffer->currentStream.get());
    ASSERT_NE(nullptr, mockExCmdBuffer->currentStream->getGraphicsAllocation());
    uint64_t exCmdBufferGpuAddr = mockExCmdBuffer->currentStream->getGraphicsAllocation()->getGpuAddress();
    EXPECT_TRUE(commandStreamReceiver.isMadeResident(mockExCmdBuffer->currentStream->getGraphicsAllocation()));

    ASSERT_NE(nullptr, mockExCmdBuffer->experimentalAllocation);
    EXPECT_TRUE(commandStreamReceiver.isMadeResident(mockExCmdBuffer->experimentalAllocation));
    ASSERT_NE(nullptr, mockExCmdBuffer->timestamps);
    EXPECT_TRUE(commandStreamReceiver.isMadeResident(mockExCmdBuffer->timestamps));

    size_t cmbBufferOffset = mockExCmdBuffer->currentStream->getUsed();

    flushTask(commandStreamReceiver);

    //two pairs of TS
    constexpr uint32_t expectedTsOffset = 4 * sizeof(uint64_t);
    EXPECT_EQ(expectedTsOffset, mockExCmdBuffer->timestampsOffset);
    constexpr uint32_t expectedExOffset = 0;
    EXPECT_EQ(expectedExOffset, mockExCmdBuffer->experimentalAllocationOffset);

    HardwareParse hwParserCsr;
    hwParserCsr.parseCommands<FamilyType>(commandStreamReceiver.commandStream, csrCmdBufferOffset);
    GenCmdList bbList = hwParserCsr.getCommandsList<MI_BATCH_BUFFER_START>();
    MI_BATCH_BUFFER_START *bbStart = nullptr;
    exCmdBufferGpuAddr += cmbBufferOffset;
    GenCmdList::iterator it = bbList.begin();
    ASSERT_NE(bbList.end(), it);
    bbStart = reinterpret_cast<MI_BATCH_BUFFER_START *>(*it);
    ASSERT_NE(nullptr, bbStart);
    EXPECT_EQ(exCmdBufferGpuAddr, bbStart->getBatchBufferStartAddressGraphicsaddress472());
    EXPECT_EQ(MI_BATCH_BUFFER_START::SECOND_LEVEL_BATCH_BUFFER_SECOND_LEVEL_BATCH, bbStart->getSecondLevelBatchBuffer());

    PIPE_CONTROL *pipeControl = nullptr;

    HardwareParse hwParserExCmdBuffer;
    hwParserExCmdBuffer.parseCommands<FamilyType>(*mockExCmdBuffer->currentStream, cmbBufferOffset);
    it = hwParserExCmdBuffer.cmdList.begin();
    GenCmdList::iterator end = hwParserExCmdBuffer.cmdList.end();
    //2nd PIPE_CONTROL
    it++;
    uint64_t timeStampAddress = mockExCmdBuffer->timestamps->getGpuAddress() + 2 * sizeof(uint64_t);
    uint32_t expectedTsAddress = static_cast<uint32_t>(timeStampAddress & 0x0000FFFFFFFFULL);
    uint32_t expectedTsAddressHigh = static_cast<uint32_t>(timeStampAddress >> 32);
    ASSERT_NE(end, it);
    pipeControl = genCmdCast<PIPE_CONTROL *>(*it);
    ASSERT_NE(nullptr, pipeControl);
    EXPECT_EQ(1u, pipeControl->getCommandStreamerStallEnable());
    EXPECT_EQ(PIPE_CONTROL::POST_SYNC_OPERATION_WRITE_TIMESTAMP, pipeControl->getPostSyncOperation());
    EXPECT_EQ(expectedTsAddress, pipeControl->getAddress());
    EXPECT_EQ(expectedTsAddressHigh, pipeControl->getAddressHigh());
    //omit SEMAPHORE_WAIT and 3rd PIPE_CONTROL
    it++;
    it++;
    //get 4th PIPE_CONTROL
    timeStampAddress = mockExCmdBuffer->timestamps->getGpuAddress() + 3 * sizeof(uint64_t);
    expectedTsAddress = static_cast<uint32_t>(timeStampAddress & 0x0000FFFFFFFFULL);
    expectedTsAddressHigh = static_cast<uint32_t>(timeStampAddress >> 32);
    it++;
    ASSERT_NE(end, it);
    pipeControl = genCmdCast<PIPE_CONTROL *>(*it);
    ASSERT_NE(nullptr, pipeControl);
    EXPECT_EQ(1u, pipeControl->getCommandStreamerStallEnable());
    EXPECT_EQ(PIPE_CONTROL::POST_SYNC_OPERATION_WRITE_TIMESTAMP, pipeControl->getPostSyncOperation());
    EXPECT_EQ(expectedTsAddress, pipeControl->getAddress());
    EXPECT_EQ(expectedTsAddressHigh, pipeControl->getAddressHigh());
}

HWTEST_F(MockExperimentalCommandBufferTest, givenEnabledExperimentalCmdBufferWhenMemoryManagerAlreadyStoresAllocationThenUseItForLinearSteam) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();
    commandStreamReceiver.storeMakeResidentAllocations = true;
    MemoryManager *memoryManager = commandStreamReceiver.getMemoryManager();

    //Make two allocations, since CSR will try to reuse it also
    auto allocation = memoryManager->allocateGraphicsMemory(3 * MemoryConstants::pageSize);
    memoryManager->storeAllocation(std::unique_ptr<GraphicsAllocation>(allocation), REUSABLE_ALLOCATION);
    allocation = memoryManager->allocateGraphicsMemory(3 * MemoryConstants::pageSize);
    memoryManager->storeAllocation(std::unique_ptr<GraphicsAllocation>(allocation), REUSABLE_ALLOCATION);

    MockExperimentalCommandBuffer *mockExCmdBuffer = static_cast<MockExperimentalCommandBuffer *>(commandStreamReceiver.experimentalCmdBuffer.get());

    flushTask(commandStreamReceiver);

    ASSERT_NE(nullptr, mockExCmdBuffer->currentStream.get());
    ASSERT_NE(nullptr, mockExCmdBuffer->currentStream->getGraphicsAllocation());
    EXPECT_EQ(allocation->getUnderlyingBuffer(), mockExCmdBuffer->currentStream->getGraphicsAllocation()->getUnderlyingBuffer());

    EXPECT_TRUE(commandStreamReceiver.isMadeResident(mockExCmdBuffer->currentStream->getGraphicsAllocation()));
}

HWTEST_F(MockExperimentalCommandBufferTest, givenEnabledExperimentalCmdBufferWhenLinearStreamIsExhaustedThenStoreOldAllocationForReuseAndObtainNewAllocationForLinearStream) {
    using MI_BATCH_BUFFER_START = typename FamilyType::MI_BATCH_BUFFER_START;

    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();
    commandStreamReceiver.storeMakeResidentAllocations = true;

    MockExperimentalCommandBuffer *mockExCmdBuffer = static_cast<MockExperimentalCommandBuffer *>(commandStreamReceiver.experimentalCmdBuffer.get());

    flushTask(commandStreamReceiver);
    size_t csrCmdBufferOffset = commandStreamReceiver.commandStream.getUsed();

    ASSERT_NE(nullptr, mockExCmdBuffer->currentStream.get());
    ASSERT_NE(nullptr, mockExCmdBuffer->currentStream->getGraphicsAllocation());
    uintptr_t oldCmdBufferAddress = reinterpret_cast<uintptr_t>(mockExCmdBuffer->currentStream->getGraphicsAllocation());
    uint64_t oldExCmdBufferGpuAddr = mockExCmdBuffer->currentStream->getGraphicsAllocation()->getGpuAddress();
    //leave space for single DWORD
    mockExCmdBuffer->currentStream->getSpace(mockExCmdBuffer->currentStream->getAvailableSpace() - sizeof(uint32_t));

    HardwareParse hwParserCsr;
    hwParserCsr.parseCommands<FamilyType>(commandStreamReceiver.commandStream, 0);
    GenCmdList bbList = hwParserCsr.getCommandsList<MI_BATCH_BUFFER_START>();
    MI_BATCH_BUFFER_START *bbStart = nullptr;
    GenCmdList::iterator it = bbList.begin();
    ASSERT_NE(bbList.end(), it);
    bbStart = reinterpret_cast<MI_BATCH_BUFFER_START *>(*it);
    ASSERT_NE(nullptr, bbStart);
    EXPECT_EQ(oldExCmdBufferGpuAddr, bbStart->getBatchBufferStartAddressGraphicsaddress472());
    EXPECT_EQ(MI_BATCH_BUFFER_START::SECOND_LEVEL_BATCH_BUFFER_SECOND_LEVEL_BATCH, bbStart->getSecondLevelBatchBuffer());

    flushTask(commandStreamReceiver);

    ASSERT_NE(nullptr, mockExCmdBuffer->currentStream.get());
    ASSERT_NE(nullptr, mockExCmdBuffer->currentStream->getGraphicsAllocation());
    EXPECT_TRUE(commandStreamReceiver.isMadeResident(mockExCmdBuffer->currentStream->getGraphicsAllocation()));
    uintptr_t newCmdBufferAddress = reinterpret_cast<uintptr_t>(mockExCmdBuffer->currentStream->getGraphicsAllocation());
    uint64_t newExCmdBufferGpuAddr = mockExCmdBuffer->currentStream->getGraphicsAllocation()->getGpuAddress();

    EXPECT_NE(oldCmdBufferAddress, newCmdBufferAddress);
    EXPECT_NE(oldExCmdBufferGpuAddr, newExCmdBufferGpuAddr);

    hwParserCsr.TearDown();
    hwParserCsr.parseCommands<FamilyType>(commandStreamReceiver.commandStream, csrCmdBufferOffset);
    bbList = hwParserCsr.getCommandsList<MI_BATCH_BUFFER_START>();
    bbStart = nullptr;
    it = bbList.begin();
    ASSERT_NE(bbList.end(), it);
    bbStart = reinterpret_cast<MI_BATCH_BUFFER_START *>(*it);
    ASSERT_NE(nullptr, bbStart);
    EXPECT_EQ(newExCmdBufferGpuAddr, bbStart->getBatchBufferStartAddressGraphicsaddress472());
    EXPECT_EQ(MI_BATCH_BUFFER_START::SECOND_LEVEL_BATCH_BUFFER_SECOND_LEVEL_BATCH, bbStart->getSecondLevelBatchBuffer());
}

HWTEST_F(ExperimentalCommandBufferTest, givenEnabledExperimentalCmdBufferWhenCommandStreamReceiverIsCreatedThenExperimentalCmdBufferIsNotNull) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();
    EXPECT_NE(nullptr, commandStreamReceiver.experimentalCmdBuffer.get());
}

HWTEST_F(ExperimentalCommandBufferTest, givenEnabledExperimentalCmdBufferWhenCommandStreamReceiverIsFlushedThenExpectPrintAfterDtor) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();
    flushTask(commandStreamReceiver);

    //forced dtor to get printed timestamps
    testing::internal::CaptureStdout();
    commandStreamReceiver.setExperimentalCmdBuffer(std::move(std::unique_ptr<ExperimentalCommandBuffer>(nullptr)));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_STRNE(output.c_str(), "");
}

HWTEST_F(ExperimentalCommandBufferTest, givenEnabledExperimentalCmdBufferWhenCommandStreamReceiverIsNotFlushedThenExpectNoPrintAfterDtor) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    //forced dtor to try to get printed timestamps
    testing::internal::CaptureStdout();
    commandStreamReceiver.setExperimentalCmdBuffer(std::move(std::unique_ptr<ExperimentalCommandBuffer>(nullptr)));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_STREQ(output.c_str(), "");
}