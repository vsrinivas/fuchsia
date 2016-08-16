// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock/mock_msd.h"
#include "sys_driver/magma_system_connection.h"
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

TEST(MagmaSystemBuffer, Create)
{
    auto scoped_bufmgr = MsdMockBufferManager::ScopedMockBufferManager(
        std::unique_ptr<MsdMockBufferManager_Create>(new MsdMockBufferManager_Create()));

    auto bufmgr = static_cast<MsdMockBufferManager_Create*>(scoped_bufmgr.get());

    auto msd_drv = msd_driver_create();
    auto msd_dev = msd_driver_create_device(msd_drv, nullptr);
    auto dev = MagmaSystemDevice(msd_device_unique_ptr_t(msd_dev, &msd_device_destroy));
    auto connection = dev.Open(0);
    ASSERT_NE(connection, nullptr);

    EXPECT_FALSE(bufmgr->has_created_buffer());
    EXPECT_FALSE(bufmgr->has_destroyed_buffer());

    {
        auto buf = connection->AllocateBuffer(0);
        EXPECT_FALSE(bufmgr->has_created_buffer());
        EXPECT_FALSE(bufmgr->has_destroyed_buffer());
    }

    {
        auto buf = connection->AllocateBuffer(256);
        EXPECT_TRUE(bufmgr->has_created_buffer());
        EXPECT_FALSE(bufmgr->has_destroyed_buffer());

        connection->FreeBuffer(buf->handle());
    }
    EXPECT_TRUE(bufmgr->has_created_buffer());
    EXPECT_TRUE(bufmgr->has_destroyed_buffer());

    msd_driver_destroy(msd_drv);
}