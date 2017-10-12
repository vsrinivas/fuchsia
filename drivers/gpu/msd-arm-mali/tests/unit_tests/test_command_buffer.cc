// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/command_buffer_helper.h"
#include "helper/platform_device_helper.h"
#include "magma_arm_mali_types.h"
#include "sys_driver/magma_driver.h"
#include "sys_driver/magma_system_connection.h"
#include "sys_driver/magma_system_context.h"
#include "gtest/gtest.h"

namespace {

class Test {
public:
    void TestInvalidCoreRequirements()
    {
        auto ctx = InitializeContext();
        ASSERT_TRUE(ctx);
        BatchBuffer buffer = MakeBatchBuffer();

        *buffer.size_ptr = 1;
        magma_arm_mali_atom* atom = buffer.atom_ptr;
        atom->atom_number = 0;
        atom->core_requirements = 0xff;

        auto command_buffer = MakeCommandBuffer(buffer.id);

        magma::Status status = ctx->ExecuteCommandBuffer(std::move(command_buffer));
        EXPECT_EQ(MAGMA_STATUS_OK, status.get());
    }

    void TestEmpty()
    {
        auto ctx = InitializeContext();
        ASSERT_TRUE(ctx);

        BatchBuffer buffer = MakeBatchBuffer();
        *buffer.size_ptr = 0;

        auto command_buffer = MakeCommandBuffer(buffer.id);

        magma::Status status = ctx->ExecuteCommandBuffer(std::move(command_buffer));
        EXPECT_EQ(MAGMA_STATUS_OK, status.get());
    }

    void TestValid()
    {
        auto ctx = InitializeContext();
        ASSERT_TRUE(ctx);

        BatchBuffer buffer = MakeBatchBuffer();
        *buffer.size_ptr = 1;
        magma_arm_mali_atom* atom = buffer.atom_ptr;
        atom->atom_number = 0;
        atom->core_requirements = 1;

        auto command_buffer = MakeCommandBuffer(buffer.id);

        magma::Status status = ctx->ExecuteCommandBuffer(std::move(command_buffer));
        EXPECT_EQ(MAGMA_STATUS_OK, status.get());
    }

    void TestTooSmall()
    {
        auto ctx = InitializeContext();
        ASSERT_TRUE(ctx);

        BatchBuffer buffer = MakeBatchBuffer();
        *buffer.size_ptr =
            buffer.buffer->platform_buffer()->size() / sizeof(magma_arm_mali_atom) + 1;

        auto command_buffer = MakeCommandBuffer(buffer.id);

        magma::Status status = ctx->ExecuteCommandBuffer(std::move(command_buffer));
        EXPECT_EQ(MAGMA_STATUS_INVALID_ARGS, status.get());
    }

    void TestOverflow()
    {
        auto ctx = InitializeContext();
        ASSERT_TRUE(ctx);
        BatchBuffer buffer = MakeBatchBuffer();

        *buffer.size_ptr = UINT64_MAX / sizeof(magma_arm_mali_atom) + 1;

        auto command_buffer = MakeCommandBuffer(buffer.id);

        magma::Status status = ctx->ExecuteCommandBuffer(std::move(command_buffer));
        EXPECT_EQ(MAGMA_STATUS_INVALID_ARGS, status.get());
    }

private:
    struct BatchBuffer {
        std::unique_ptr<MagmaSystemBuffer> buffer;
        uint64_t id;
        uint64_t* size_ptr;
        magma_arm_mali_atom* atom_ptr;
    };

    std::unique_ptr<magma::PlatformBuffer> MakeCommandBuffer(uint32_t batch_buffer_id)
    {
        auto buffer = magma::PlatformBuffer::Create(1024, "command-buffer");
        void* addr;
        buffer->MapCpu(&addr);
        magma_system_command_buffer* command_buffer =
            static_cast<magma_system_command_buffer*>(addr);
        command_buffer->batch_buffer_resource_index = 0;
        command_buffer->num_resources = 1;
        command_buffer->wait_semaphore_count = 0;
        command_buffer->signal_semaphore_count = 0;
        magma_system_exec_resource* exec_resources =
            reinterpret_cast<magma_system_exec_resource*>(command_buffer + 1);
        for (size_t i = 0; i < 10; i++) {
            exec_resources[i].buffer_id = i;
            exec_resources[i].num_relocations = 0;
            exec_resources[i].offset = 0;
            exec_resources[i].length = 0;
        }
        exec_resources[0].buffer_id = batch_buffer_id;
        buffer->UnmapCpu();
        return buffer;
    }

    BatchBuffer MakeBatchBuffer()
    {
        BatchBuffer buffer;
        buffer.buffer =
            MagmaSystemBuffer::Create(magma::PlatformBuffer::Create(100, "command-buffer-batch"));
        DASSERT(buffer.buffer);
        uint32_t duplicate_handle;
        bool success = buffer.buffer->platform_buffer()->duplicate_handle(&duplicate_handle);
        DASSERT(success);
        success = connection_->ImportBuffer(duplicate_handle, &buffer.id);
        DASSERT(success);
        void* addr;
        buffer.buffer->platform_buffer()->MapCpu(&addr);
        buffer.size_ptr = static_cast<uint64_t*>(addr);
        buffer.atom_ptr = reinterpret_cast<magma_arm_mali_atom*>(buffer.size_ptr + 1);
        return buffer;
    }

    MagmaSystemContext* InitializeContext()
    {
        msd_drv_ = msd_driver_unique_ptr_t(msd_driver_create(), &msd_driver_destroy);
        if (!msd_drv_)
            return DRETP(nullptr, "failed to create msd driver");

        msd_driver_configure(msd_drv_.get(), MSD_DRIVER_CONFIG_TEST_NO_DEVICE_THREAD);

        platform_device_ = TestPlatformDevice::GetInstance();
        if (!platform_device_)
            DLOG("TestCommandBuffer: No platform device");
        auto msd_dev = msd_driver_create_device(
            msd_drv_.get(), platform_device_ ? platform_device_->GetDeviceHandle() : nullptr);
        if (!msd_dev)
            return DRETP(nullptr, "failed to create msd device");
        system_dev_ = std::shared_ptr<MagmaSystemDevice>(
            MagmaSystemDevice::Create(MsdDeviceUniquePtr(msd_dev)));
        uint32_t ctx_id = 0;
        auto msd_connection_t = msd_device_open(msd_dev, 0);
        if (!msd_connection_t)
            return DRETP(nullptr, "msd_device_open failed");
        connection_ = std::unique_ptr<MagmaSystemConnection>(new MagmaSystemConnection(
            system_dev_, MsdConnectionUniquePtr(msd_connection_t), MAGMA_CAPABILITY_RENDERING));
        if (!connection_)
            return DRETP(nullptr, "failed to connect to msd device");
        connection_->CreateContext(ctx_id);
        auto ctx = connection_->LookupContext(ctx_id);
        if (!ctx)
            return DRETP(nullptr, "failed to create context");
        return ctx;
    }

    msd_driver_unique_ptr_t msd_drv_;
    magma::PlatformDevice* platform_device_;
    std::shared_ptr<MagmaSystemDevice> system_dev_;
    std::unique_ptr<MagmaSystemConnection> connection_;
};

TEST(CommandBuffer, TestTooSmall) { ::Test().TestTooSmall(); }
TEST(CommandBuffer, TestEmpty) { ::Test().TestEmpty(); }
TEST(CommandBuffer, TestInvalidCoreRequirements) { ::Test().TestInvalidCoreRequirements(); }
TEST(CommandBuffer, TestOverflow) { ::Test().TestOverflow(); }
}
