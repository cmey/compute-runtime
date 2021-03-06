/*
 * Copyright (c) 2017 - 2018, Intel Corporation
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
#include "gtest/gtest.h"
#include "CL/cl.h"
#include "runtime/command_queue/command_queue.h"
#include "unit_tests/mocks/mock_context.h"

namespace OCLRT {
class Device;

struct CommandQueueHwFixture {
    CommandQueueHwFixture();

    CommandQueue *createCommandQueue(
        Device *device,
        cl_command_queue_properties properties);

    virtual void SetUp();
    virtual void SetUp(Device *_pDevice, cl_command_queue_properties properties);

    virtual void TearDown();

    CommandQueue *pCmdQ;
    MockContext *context;
};

struct OOQueueFixture : public CommandQueueHwFixture {
    typedef CommandQueueHwFixture BaseClass;

    virtual void SetUp(Device *_pDevice, cl_command_queue_properties _properties) override {
        ASSERT_NE(nullptr, _pDevice);
        BaseClass::pCmdQ = BaseClass::createCommandQueue(_pDevice, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE);
        ASSERT_NE(nullptr, BaseClass::pCmdQ);
    }
};

struct CommandQueueFixture {
    CommandQueueFixture();

    virtual void SetUp(
        Context *context,
        Device *device,
        cl_command_queue_properties properties);
    virtual void TearDown();

    CommandQueue *createCommandQueue(
        Context *context,
        Device *device,
        cl_command_queue_properties properties);

    CommandQueue *pCmdQ;
};

static const cl_command_queue_properties AllCommandQueueProperties[] = {
    0,
    CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE,
    CL_QUEUE_ON_DEVICE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE,
    CL_QUEUE_ON_DEVICE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_ON_DEVICE_DEFAULT,
    CL_QUEUE_PROFILING_ENABLE,
    CL_QUEUE_PROFILING_ENABLE | CL_QUEUE_ON_DEVICE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE,
    CL_QUEUE_PROFILING_ENABLE | CL_QUEUE_ON_DEVICE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_ON_DEVICE_DEFAULT};

static const cl_command_queue_properties DefaultCommandQueueProperties[] = {
    0,
    CL_QUEUE_PROFILING_ENABLE,
};
} // namespace OCLRT
