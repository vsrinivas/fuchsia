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

#include "magma_system_device.h"
#include "mock_msd.h"
#include "gtest/gtest.h"

class MsdMockBufferManager_Create : public MsdMockBufferManager {
public:
    MsdMockBufferManager_Create() : has_created_buffer_(false), has_destroyed_buffer_(false) {}

    MsdMockBuffer* CreateBuffer(struct msd_platform_buffer* platform_buf)
    {
        has_created_buffer_ = true;
        return MsdMockBufferManager::CreateBuffer(platform_buf);
    }

    void DestroyBuffer(MsdMockBuffer* buf)
    {
        has_destroyed_buffer_ = true;
        MsdMockBufferManager::DestroyBuffer(buf);
    }

    bool has_created_buffer() { return has_created_buffer_; };
    bool has_destroyed_buffer() { return has_destroyed_buffer_; };

private:
    bool has_created_buffer_;
    bool has_destroyed_buffer_;
};

TEST(Magma, MagmaSystemBuffer_Create)
{
    auto scoped_bufmgr = MsdMockBufferManager::ScopedMockBufferManager(
        std::unique_ptr<MsdMockBufferManager_Create>(new MsdMockBufferManager_Create()));

    auto bufmgr = static_cast<MsdMockBufferManager_Create*>(scoped_bufmgr.get());

    auto msd_drv = msd_driver_create();
    auto msd_dev = msd_driver_create_device(msd_drv, nullptr);
    auto dev = MagmaSystemDevice(msd_dev);

    EXPECT_FALSE(bufmgr->has_created_buffer());
    EXPECT_FALSE(bufmgr->has_destroyed_buffer());

    {
        auto buf = dev.AllocateBuffer(0);
        EXPECT_FALSE(bufmgr->has_created_buffer());
        EXPECT_FALSE(bufmgr->has_destroyed_buffer());
    }

    {
        auto buf = dev.AllocateBuffer(256);
        EXPECT_TRUE(bufmgr->has_created_buffer());
        EXPECT_FALSE(bufmgr->has_destroyed_buffer());

        dev.FreeBuffer(buf->handle());
    }
    EXPECT_TRUE(bufmgr->has_created_buffer());
    EXPECT_TRUE(bufmgr->has_destroyed_buffer());

    // TODO(MA-25) msd device should be destroyed as part of the MagmaSystemDevice destructor
    msd_driver_destroy_device(msd_dev);
    msd_driver_destroy(msd_drv);
}