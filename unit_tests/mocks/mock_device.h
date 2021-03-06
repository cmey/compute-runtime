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

#pragma once
#include "runtime/device/device.h"
#include "runtime/execution_environment/execution_environment.h"
#include "runtime/helpers/hw_info.h"
#include "runtime/memory_manager/memory_manager.h"
#include "runtime/memory_manager/os_agnostic_memory_manager.h"
#include "unit_tests/libult/ult_command_stream_receiver.h"
#include "unit_tests/mocks/mock_memory_manager.h"

namespace OCLRT {
class OSTime;
class MemoryManager;
class MockMemoryManager;

extern CommandStreamReceiver *createCommandStream(const HardwareInfo *pHwInfo, ExecutionEnvironment &executionEnvironment);

class MockDevice : public Device {
  public:
    using Device::createDeviceImpl;
    using Device::executionEnvironment;
    using Device::initializeCaps;

    void setOSTime(OSTime *osTime);
    void setDriverInfo(DriverInfo *driverInfo);
    bool hasDriverInfo();

    bool getCpuTime(uint64_t *timeStamp) { return true; };
    void *peekSlmWindowStartAddress() const {
        return this->slmWindowStartAddress;
    }
    MockDevice(const HardwareInfo &hwInfo);
    MockDevice(const HardwareInfo &hwInfo, ExecutionEnvironment *executionEnvironment);

    DeviceInfo *getDeviceInfoToModify() {
        return &this->deviceInfo;
    }

    void initializeCaps() override {
        Device::initializeCaps();
    }

    void setPreemptionMode(PreemptionMode mode) {
        preemptionMode = mode;
    }

    const WhitelistedRegisters &getWhitelistedRegisters() override {
        if (forceWhitelistedRegs) {
            return mockWhitelistedRegs;
        }
        return Device::getWhitelistedRegisters();
    }

    const WorkaroundTable *getWaTable() const override { return &mockWaTable; }

    void setForceWhitelistedRegs(bool force, WhitelistedRegisters *mockRegs = nullptr) {
        forceWhitelistedRegs = force;
        if (mockRegs) {
            mockWhitelistedRegs = *mockRegs;
        }
    }

    void injectMemoryManager(MockMemoryManager *);

    void setPerfCounters(PerformanceCounters *perfCounters) {
        performanceCounters = std::unique_ptr<PerformanceCounters>(perfCounters);
    }
    void setMemoryManager(MemoryManager *memoryManager);

    template <typename T>
    UltCommandStreamReceiver<T> &getUltCommandStreamReceiver() {
        return reinterpret_cast<UltCommandStreamReceiver<T> &>(getCommandStreamReceiver());
    }

    void resetCommandStreamReceiver(CommandStreamReceiver *newCsr);

    GraphicsAllocation *getTagAllocation() { return this->getCommandStreamReceiver().getTagAllocation(); }

    void setSourceLevelDebuggerActive(bool active) {
        this->deviceInfo.sourceLevelDebuggerActive = active;
    }

    template <typename T>
    static T *createWithExecutionEnvironment(const HardwareInfo *pHwInfo, ExecutionEnvironment *executionEnvironment) {
        pHwInfo = getDeviceInitHwInfo(pHwInfo);
        T *device = new T(*pHwInfo, executionEnvironment);
        executionEnvironment->memoryManager = std::move(device->mockMemoryManager);
        return createDeviceInternals(pHwInfo, device);
    }

    template <typename T>
    static T *createWithNewExecutionEnvironment(const HardwareInfo *pHwInfo) {
        return createWithExecutionEnvironment<T>(pHwInfo, new ExecutionEnvironment());
    }

    void allocatePreemptionAllocationIfNotPresent() {
        if (this->preemptionAllocation == nullptr) {
            if (preemptionMode == PreemptionMode::MidThread || isSourceLevelDebuggerActive()) {
                size_t requiredSize = hwInfo.capabilityTable.requiredPreemptionSurfaceSize;
                size_t alignment = 256 * MemoryConstants::kiloByte;
                bool uncacheable = getWaTable()->waCSRUncachable;
                this->preemptionAllocation = executionEnvironment->memoryManager->allocateGraphicsMemory(requiredSize, alignment, false, uncacheable);
                executionEnvironment->commandStreamReceiver->setPreemptionCsrAllocation(preemptionAllocation);
            }
        }
    }
    std::unique_ptr<MemoryManager> mockMemoryManager;

  private:
    bool forceWhitelistedRegs = false;
    WhitelistedRegisters mockWhitelistedRegs = {0};
    WorkaroundTable mockWaTable = {};
};

template <>
inline Device *MockDevice::createWithNewExecutionEnvironment<Device>(const HardwareInfo *pHwInfo) {
    return Device::create<Device>(pHwInfo, new ExecutionEnvironment);
}

class FailMemoryManager : public MockMemoryManager {
  public:
    FailMemoryManager();
    FailMemoryManager(int32_t fail);
    virtual ~FailMemoryManager() override {
        if (agnostic) {
            for (auto alloc : allocations) {
                agnostic->freeGraphicsMemory(alloc);
            }
            delete agnostic;
        }
    };
    GraphicsAllocation *allocateGraphicsMemory(size_t size, size_t alignment, bool forcePin, bool uncacheable) override {
        if (fail <= 0) {
            return nullptr;
        }
        fail--;
        GraphicsAllocation *alloc = agnostic->allocateGraphicsMemory(size, alignment, forcePin, uncacheable);
        allocations.push_back(alloc);
        return alloc;
    };
    GraphicsAllocation *allocateGraphicsMemory64kb(size_t size, size_t alignment, bool forcePin, bool preferRenderCompressed) override {
        return nullptr;
    };
    GraphicsAllocation *allocateGraphicsMemory(size_t size, const void *ptr) override {
        return nullptr;
    };
    GraphicsAllocation *allocate32BitGraphicsMemory(size_t size, const void *ptr, AllocationOrigin allocationOrigin) override {
        return nullptr;
    };
    GraphicsAllocation *createGraphicsAllocationFromSharedHandle(osHandle handle, bool requireSpecificBitness, bool reuseBO) override {
        return nullptr;
    };
    GraphicsAllocation *createGraphicsAllocationFromNTHandle(void *handle) override {
        return nullptr;
    };
    void freeGraphicsMemoryImpl(GraphicsAllocation *gfxAllocation) override{};
    void *lockResource(GraphicsAllocation *gfxAllocation) override { return nullptr; };
    void unlockResource(GraphicsAllocation *gfxAllocation) override{};

    MemoryManager::AllocationStatus populateOsHandles(OsHandleStorage &handleStorage) override {
        return AllocationStatus::Error;
    };
    void cleanOsHandles(OsHandleStorage &handleStorage) override{};

    uint64_t getSystemSharedMemory() override {
        return 0;
    };

    uint64_t getMaxApplicationAddress() override {
        return MemoryConstants::max32BitAppAddress;
    };

    GraphicsAllocation *createGraphicsAllocation(OsHandleStorage &handleStorage, size_t hostPtrSize, const void *hostPtr) override {
        return nullptr;
    };
    GraphicsAllocation *allocateGraphicsMemoryForImage(ImageInfo &imgInfo, Gmm *gmm) override {
        return nullptr;
    }
    int32_t fail;
    OsAgnosticMemoryManager *agnostic;
    std::vector<GraphicsAllocation *> allocations;
};

class FailDevice : public MockDevice {
  public:
    FailDevice(const HardwareInfo &hwInfo, ExecutionEnvironment *executionEnvironment)
        : MockDevice(hwInfo, executionEnvironment) {
        this->mockMemoryManager.reset(new FailMemoryManager);
    }
};

class FailDeviceAfterOne : public MockDevice {
  public:
    FailDeviceAfterOne(const HardwareInfo &hwInfo, ExecutionEnvironment *executionEnvironment)
        : MockDevice(hwInfo, executionEnvironment) {
        this->mockMemoryManager.reset(new FailMemoryManager(1));
    }
};

class MockAlignedMallocManagerDevice : public MockDevice {
  public:
    MockAlignedMallocManagerDevice(const HardwareInfo &hwInfo, ExecutionEnvironment *executionEnvironment);
};

} // namespace OCLRT
