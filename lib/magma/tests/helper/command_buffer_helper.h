// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_device.h"
#include "sys_driver/magma_driver.h"
#include "sys_driver/magma_system_connection.h"
#include "sys_driver/magma_system_context.h"
#include "gtest/gtest.h"

// a class to create and own the command buffer were trying to execute
class CommandBufferHelper {
public:
    ~CommandBufferHelper()
    {
        bool success = buffer_->platform_buffer()->UnmapCpu();
        DASSERT(success);
    }

    static std::unique_ptr<CommandBufferHelper>
    Create(magma::PlatformDevice* platform_device = nullptr)
    {
        auto msd_drv = msd_driver_unique_ptr_t(msd_driver_create(), &msd_driver_destroy);
        if (!msd_drv)
            return DRETP(nullptr, "failed to create msd driver");

        msd_driver_configure(msd_drv.get(), MSD_DRIVER_CONFIG_TEST_NO_DEVICE_THREAD);

        auto msd_dev = msd_driver_create_device(
            msd_drv.get(), platform_device ? platform_device->GetDeviceHandle() : nullptr);
        if (!msd_dev)
            return DRETP(nullptr, "failed to create msd device");
        auto dev = std::make_shared<MagmaSystemDevice>(
            msd_device_unique_ptr_t(msd_dev, &msd_device_destroy));
        uint32_t ctx_id = 0;
        auto msd_connection_t = msd_device_open(msd_dev, 0);
        if (!msd_connection_t)
            return DRETP(nullptr, "msd_device_open failed");
        auto connection = std::unique_ptr<MagmaSystemConnection>(new MagmaSystemConnection(
            dev, MsdConnectionUniquePtr(msd_connection_t), MAGMA_CAPABILITY_RENDERING));
        if (!connection)
            return DRETP(nullptr, "failed to connect to msd device");
        connection->CreateContext(ctx_id);
        auto ctx = connection->LookupContext(ctx_id);
        if (!msd_dev)
            return DRETP(nullptr, "failed to create context");

        return std::unique_ptr<CommandBufferHelper>(new CommandBufferHelper(
            std::move(msd_drv), std::move(dev), std::move(connection), ctx));
    }

    static constexpr uint32_t kNumResources = 3;
    static constexpr uint32_t kBufferSize = PAGE_SIZE * 2;

    static constexpr uint32_t kWaitSemaphoreCount = 2;
    static constexpr uint32_t kSignalSemaphoreCount = 2;

    std::vector<MagmaSystemBuffer*>& resources() { return resources_; }
    std::vector<msd_buffer_t*>& msd_resources() { return msd_resources_; }

    msd_context_t* ctx() { return ctx_->msd_ctx(); }
    MagmaSystemDevice* dev() { return dev_.get(); }
    MagmaSystemBuffer* buffer() { return buffer_.get(); }

    msd_semaphore_t** msd_wait_semaphores() { return msd_wait_semaphores_.data(); }
    msd_semaphore_t** msd_signal_semaphores() { return msd_signal_semaphores_.data(); }

    magma_system_command_buffer* abi_cmd_buf()
    {
        DASSERT(buffer_data_);
        return reinterpret_cast<magma_system_command_buffer*>(buffer_data_);
    }

    uint64_t* abi_wait_semaphore_ids() { return reinterpret_cast<uint64_t*>(abi_cmd_buf() + 1); }

    uint64_t* abi_signal_semaphore_ids()
    {
        return reinterpret_cast<uint64_t*>(abi_wait_semaphore_ids() + kWaitSemaphoreCount);
    }

    magma_system_exec_resource* abi_resources()
    {
        return reinterpret_cast<magma_system_exec_resource*>(abi_signal_semaphore_ids() +
                                                             kSignalSemaphoreCount);
    }

    magma_system_relocation_entry* abi_relocations()
    {
        return reinterpret_cast<magma_system_relocation_entry*>(abi_resources() + kNumResources);
    }

    bool Execute()
    {
        if (!ctx_->ExecuteCommandBuffer(buffer_))
            return false;
        for (uint32_t i = 0; i < wait_semaphores_.size(); i++) {
            wait_semaphores_[i]->Signal();
        }
        return true;
    }

    bool ExecuteAndWait()
    {
        if (!Execute())
            return false;

        for (uint32_t i = 0; i < signal_semaphores_.size(); i++) {
            if (!signal_semaphores_[i]->Wait(5000))
                return DRETF(false, "timed out waiting for signal semaphore %d", i);
        }
        return true;
    }

private:
    CommandBufferHelper(msd_driver_unique_ptr_t msd_drv, std::shared_ptr<MagmaSystemDevice> dev,
                        std::unique_ptr<MagmaSystemConnection> connection, MagmaSystemContext* ctx)
        : msd_drv_(std::move(msd_drv)), dev_(std::move(dev)), connection_(std::move(connection)),
          ctx_(ctx)
    {
        uint64_t buffer_size = sizeof(magma_system_command_buffer) +
                               sizeof(uint64_t) * kSignalSemaphoreCount +
                               sizeof(magma_system_exec_resource) * kNumResources +
                               sizeof(magma_system_relocation_entry) * (kNumResources - 1);

        buffer_ = MagmaSystemBuffer::Create(magma::PlatformBuffer::Create(buffer_size));
        DASSERT(buffer_);

        DLOG("CommandBuffer backing buffer: %p", buffer_->platform_buffer());

        bool success = buffer_->platform_buffer()->MapCpu(&buffer_data_);
        DASSERT(success);
        DASSERT(buffer_data_);

        abi_cmd_buf()->batch_buffer_resource_index = 0;
        abi_cmd_buf()->batch_start_offset = 0;
        abi_cmd_buf()->num_resources = kNumResources;
        abi_cmd_buf()->wait_semaphore_count = kWaitSemaphoreCount;
        abi_cmd_buf()->signal_semaphore_count = kSignalSemaphoreCount;

        // batch buffer
        {
            auto batch_buf = &abi_resources()[0];
            auto buffer = MagmaSystemBuffer::Create(magma::PlatformBuffer::Create(kBufferSize));
            DASSERT(buffer);
            uint32_t duplicate_handle;
            success = buffer->platform_buffer()->duplicate_handle(&duplicate_handle);
            DASSERT(success);
            uint64_t id;
            success = connection_->ImportBuffer(duplicate_handle, &id);
            DASSERT(success);
            resources_.push_back(connection_->LookupBuffer(id).get());
            success = buffer->platform_buffer()->duplicate_handle(&duplicate_handle);
            DASSERT(success);
            batch_buf->buffer_id = id;
            batch_buf->offset = 0;
            batch_buf->length = buffer->platform_buffer()->size();
            batch_buf->num_relocations = kNumResources - 1;
            for (uint32_t i = 0; i < batch_buf->num_relocations; i++) {
                auto relocation = &abi_relocations()[i];
                switch (i) {
                case 0:
                    // test page boundary
                    relocation->offset = kBufferSize / 2 - sizeof(uint32_t);
                    break;
                default:
                    relocation->offset =
                        kBufferSize - ((i + 1) * 2 * sizeof(uint32_t)); // every other dword
                }
                relocation->target_resource_index = i;
                relocation->target_offset = kBufferSize / 2; // just relocate right to the middle
                relocation->read_domains_bitfield = MAGMA_DOMAIN_CPU;
                relocation->write_domains_bitfield = MAGMA_DOMAIN_CPU;
            }
        }

        // relocated buffers
        for (uint32_t i = 1; i < kNumResources; i++) {
            auto resource = &abi_resources()[i];
            auto buffer = MagmaSystemBuffer::Create(magma::PlatformBuffer::Create(kBufferSize));
            DASSERT(buffer);
            uint32_t duplicate_handle;
            success = buffer->platform_buffer()->duplicate_handle(&duplicate_handle);
            DASSERT(success);
            uint64_t id;
            success = connection_->ImportBuffer(duplicate_handle, &id);
            DASSERT(success);
            resources_.push_back(connection_->LookupBuffer(id).get());
            success = buffer->platform_buffer()->duplicate_handle(&duplicate_handle);
            DASSERT(success);
            resource->buffer_id = id;
            resource->offset = 0;
            resource->length = buffer->platform_buffer()->size();
            resource->num_relocations = 0;
        }

        for (auto resource : resources_)
            msd_resources_.push_back(resource->msd_buf());

        // wait semaphores
        for (uint32_t i = 0; i < kWaitSemaphoreCount; i++) {
            auto semaphore =
                std::shared_ptr<magma::PlatformSemaphore>(magma::PlatformSemaphore::Create());
            DASSERT(semaphore);
            uint32_t duplicate_handle;
            success = semaphore->duplicate_handle(&duplicate_handle);
            DASSERT(success);
            wait_semaphores_.push_back(semaphore);
            success = connection_->ImportObject(duplicate_handle, magma::PlatformObject::SEMAPHORE);
            DASSERT(success);
            abi_wait_semaphore_ids()[i] = semaphore->id();
            msd_wait_semaphores_.push_back(
                connection_->LookupSemaphore(semaphore->id())->msd_semaphore());
        }

        // signal semaphores
        for (uint32_t i = 0; i < kSignalSemaphoreCount; i++) {
            auto semaphore =
                std::shared_ptr<magma::PlatformSemaphore>(magma::PlatformSemaphore::Create());
            DASSERT(semaphore);
            uint32_t duplicate_handle;
            success = semaphore->duplicate_handle(&duplicate_handle);
            DASSERT(success);
            signal_semaphores_.push_back(semaphore);
            success = connection_->ImportObject(duplicate_handle, magma::PlatformObject::SEMAPHORE);
            DASSERT(success);
            abi_signal_semaphore_ids()[i] = semaphore->id();
            msd_signal_semaphores_.push_back(
                connection_->LookupSemaphore(semaphore->id())->msd_semaphore());
        }
    }

    msd_driver_unique_ptr_t msd_drv_;
    std::shared_ptr<MagmaSystemDevice> dev_;
    std::unique_ptr<MagmaSystemConnection> connection_;
    MagmaSystemContext* ctx_; // owned by the connection

    std::shared_ptr<MagmaSystemBuffer> buffer_;
    // mapped address of buffer_, do not free
    void* buffer_data_ = nullptr;

    std::vector<MagmaSystemBuffer*> resources_;
    std::vector<msd_buffer_t*> msd_resources_;

    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores_;
    std::vector<msd_semaphore_t*> msd_wait_semaphores_;
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores_;
    std::vector<msd_semaphore_t*> msd_signal_semaphores_;
};
