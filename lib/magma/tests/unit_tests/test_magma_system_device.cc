// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mock/mock_msd.h"
#include "sys-driver/magma_system.h"
#include "sys-driver/magma_system_device.h"
#include "gtest/gtest.h"

class MsdMockDevice_GetDeviceId : public MsdMockDevice {
public:
    MsdMockDevice_GetDeviceId(uint32_t device_id) : device_id_(device_id) {}

    uint32_t GetDeviceId() override { return device_id_; }

private:
    uint32_t device_id_;
};

TEST(Magma, MagmaSystemDevice_GetDeviceId)
{
    uint32_t test_id = 0xdeadbeef;

    auto msd_dev = new MsdMockDevice_GetDeviceId(test_id);
    auto dev = MagmaSystemDevice(msd_dev);

    uint32_t device_id = dev.GetDeviceId();
    // For now device_id is invalid
    EXPECT_EQ(device_id, test_id);

    // TODO(MA-25) msd device should be destroyed as part of the MagmaSystemDevice destructor
    msd_driver_destroy_device(msd_dev);
}

TEST(Magma, MagmaSystemDevice_BufferManagement)
{
    auto msd_drv = msd_driver_create();
    auto msd_dev = msd_driver_create_device(msd_drv, nullptr);
    auto dev = MagmaSystemDevice(msd_dev);

    uint64_t test_size = 4096;

    {
        // allocating a zero size buffer should fail
        EXPECT_EQ(dev.AllocateBuffer(0), nullptr);

        auto buf = dev.AllocateBuffer(test_size);
        // assert because if this fails the rest of this is gonna be bogus anyway
        ASSERT_NE(buf, nullptr);
        EXPECT_GE(buf->size(), test_size);

        auto handle = buf->handle();
        EXPECT_EQ(handle, buf->platform_buffer()->handle());

        // should be able to get the buffer by handle
        auto get_buf = dev.LookupBuffer(handle);
        EXPECT_NE(get_buf, nullptr);
        EXPECT_EQ(get_buf, buf); // they are shared ptrs after all

        // freeing the allocated buffer should work
        EXPECT_TRUE(dev.FreeBuffer(handle));

        // should no longer be able to get it from the map
        EXPECT_EQ(dev.LookupBuffer(handle), nullptr);

        // should not be able to double free it
        EXPECT_FALSE(dev.FreeBuffer(handle));
    }

    msd_driver_destroy_device(msd_dev);
    msd_driver_destroy(msd_drv);
}

class MsdMockDevice_ContextManagement : public MsdMockDevice {
public:
    MsdMockDevice_ContextManagement() {}

    MsdMockContext* CreateContext() override
    {
        active_context_count_++;
        return MsdMockDevice::CreateContext();
    }

    void DestroyContext(MsdMockContext* ctx) override
    {
        active_context_count_--;
        MsdMockDevice::DestroyContext(ctx);
    }

    uint32_t NumActiveContexts() { return active_context_count_; }

private:
    uint32_t active_context_count_;
};

TEST(Magma, MagmaSystemDevice_ContextManagement)
{
    auto msd_dev = new MsdMockDevice_ContextManagement();
    auto dev = MagmaSystemDevice(msd_dev);

    EXPECT_EQ(msd_dev->NumActiveContexts(), 0u);

    uint32_t context_id_0;
    uint32_t context_id_1;

    EXPECT_TRUE(dev.CreateContext(&context_id_0));
    EXPECT_EQ(msd_dev->NumActiveContexts(), 1u);

    EXPECT_TRUE(magma_system_create_context(&dev, &context_id_1));
    EXPECT_EQ(msd_dev->NumActiveContexts(), 2u);

    EXPECT_NE(context_id_0, context_id_1);

    EXPECT_TRUE(dev.DestroyContext(context_id_0));
    EXPECT_EQ(msd_dev->NumActiveContexts(), 1u);
    EXPECT_FALSE(dev.DestroyContext(context_id_0));

    EXPECT_TRUE(magma_system_destroy_context(&dev, context_id_1));
    EXPECT_EQ(msd_dev->NumActiveContexts(), 0u);
    EXPECT_FALSE(magma_system_destroy_context(&dev, context_id_1));

    // TODO(MA-25) msd device should be destroyed as part of the MagmaSystemDevice destructor
    msd_driver_destroy_device(msd_dev);
}
