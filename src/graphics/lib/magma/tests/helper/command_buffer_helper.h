// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "platform_pci_device.h"
#include "sys_driver/magma_driver.h"
#include "sys_driver/magma_system_connection.h"
#include "sys_driver/magma_system_context.h"

// a class to create and own the command buffer were trying to execute
class CommandBufferHelper {
 public:
  static std::unique_ptr<CommandBufferHelper> Create(
      magma::PlatformPciDevice* platform_device = nullptr) {
    auto msd_drv = msd_driver_unique_ptr_t(msd_driver_create(), &msd_driver_destroy);
    if (!msd_drv)
      return DRETP(nullptr, "failed to create msd driver");

    msd_driver_configure(msd_drv.get(), MSD_DRIVER_CONFIG_TEST_NO_DEVICE_THREAD);

    void* device_handle = platform_device ? platform_device->GetDeviceHandle() : nullptr;
    auto msd_dev = msd_driver_create_device(msd_drv.get(), device_handle);
    if (!msd_dev)
      return DRETP(nullptr, "failed to create msd device");
    auto dev = std::shared_ptr<MagmaSystemDevice>(
        MagmaSystemDevice::Create(MsdDeviceUniquePtr(msd_dev), device_handle));
    uint32_t ctx_id = 0;
    auto msd_connection_t = msd_device_open(msd_dev, 0);
    if (!msd_connection_t)
      return DRETP(nullptr, "msd_device_open failed");
    auto connection = std::unique_ptr<MagmaSystemConnection>(
        new MagmaSystemConnection(dev, MsdConnectionUniquePtr(msd_connection_t)));
    if (!connection)
      return DRETP(nullptr, "failed to connect to msd device");
    connection->CreateContext(ctx_id);
    auto ctx = connection->LookupContext(ctx_id);
    if (!msd_dev)
      return DRETP(nullptr, "failed to create context");

    return std::unique_ptr<CommandBufferHelper>(
        new CommandBufferHelper(std::move(msd_drv), std::move(dev), std::move(connection), ctx));
  }

  static constexpr uint32_t kNumResources = 3;
  static constexpr uint32_t kBufferSize = PAGE_SIZE * 2;

  static constexpr uint32_t kWaitSemaphoreCount = 2;
  static constexpr uint32_t kSignalSemaphoreCount = 2;

  std::vector<MagmaSystemBuffer*>& resources() { return resources_; }
  std::vector<msd_buffer_t*>& msd_resources() { return msd_resources_; }

  msd_context_t* ctx() { return ctx_->msd_ctx(); }
  MagmaSystemDevice* dev() { return dev_.get(); }
  MagmaSystemConnection* connection() { return connection_.get(); }

  magma::PlatformBuffer* buffer() {
    DASSERT(buffer_);
    return buffer_.get();
  }

  msd_semaphore_t** msd_wait_semaphores() { return msd_wait_semaphores_.data(); }
  msd_semaphore_t** msd_signal_semaphores() { return msd_signal_semaphores_.data(); }

  magma_command_buffer* abi_cmd_buf() {
    DASSERT(buffer_data_);
    return reinterpret_cast<magma_command_buffer*>(buffer_data_);
  }

  uint64_t* abi_wait_semaphore_ids() { return reinterpret_cast<uint64_t*>(abi_cmd_buf() + 1); }

  uint64_t* abi_signal_semaphore_ids() {
    return reinterpret_cast<uint64_t*>(abi_wait_semaphore_ids() + kWaitSemaphoreCount);
  }

  magma_exec_resource* abi_resources() {
    return reinterpret_cast<magma_exec_resource*>(abi_signal_semaphore_ids() +
                                                  kSignalSemaphoreCount);
  }

  void set_command_buffer_flags(uint64_t flags) { abi_cmd_buf()->flags = flags; }

  bool Execute() {
    auto command_buffer = std::make_unique<magma_command_buffer>(*abi_cmd_buf());
    std::vector<magma_exec_resource> resources;
    for (uint32_t i = 0; i < kNumResources; i++) {
      resources.emplace_back(abi_resources()[i]);
    }
    std::vector<uint64_t> semaphores;
    for (uint32_t i = 0; i < kWaitSemaphoreCount; i++) {
      semaphores.emplace_back(abi_wait_semaphore_ids()[i]);
    }
    for (uint32_t i = 0; i < kSignalSemaphoreCount; i++) {
      semaphores.emplace_back(abi_signal_semaphore_ids()[i]);
    }
    if (!ctx_->ExecuteCommandBufferWithResources(std::move(command_buffer), std::move(resources),
                                                 std::move(semaphores)))
      return false;

    ProcessNotifications();
    return true;
  }

  void ProcessNotifications() {
    while (notifications_.size()) {
      std::vector<msd_notification_t> processing_notifications;
      notifications_.swap(processing_notifications);

      for (auto& notification : processing_notifications) {
        static int cancel_token;

        if (notification.type == MSD_CONNECTION_NOTIFICATION_HANDLE_WAIT_CANCEL) {
          EXPECT_EQ(notification.u.handle_wait_cancel.cancel_token, &cancel_token);

        } else if (notification.type == MSD_CONNECTION_NOTIFICATION_HANDLE_WAIT) {
          notification.u.handle_wait.starter(notification.u.handle_wait.wait_context,
                                             &cancel_token);

          magma_handle_t handle_copy;
          ASSERT_TRUE(magma::PlatformHandle::duplicate_handle(notification.u.handle_wait.handle,
                                                              &handle_copy));

          auto semaphore = magma::PlatformSemaphore::Import(handle_copy);
          ASSERT_TRUE(semaphore);
          semaphore->Signal();

          notification.u.handle_wait.completer(notification.u.handle_wait.wait_context,
                                               MAGMA_STATUS_OK, notification.u.handle_wait.handle);
        }
      }
    }
  }

  bool ExecuteAndWait() {
    if (!Execute())
      return false;

    for (uint32_t i = 0; i < signal_semaphores_.size(); i++) {
      if (!signal_semaphores_[i]->Wait(5000))
        return DRETF(false, "timed out waiting for signal semaphore %d", i);
    }
    return true;
  }

  static void NotificationCallback(void* token, msd_notification_t* notification) {
    auto helper = reinterpret_cast<CommandBufferHelper*>(token);
    helper->notifications_.push_back(*notification);
  }

 private:
  CommandBufferHelper(msd_driver_unique_ptr_t msd_drv, std::shared_ptr<MagmaSystemDevice> dev,
                      std::unique_ptr<MagmaSystemConnection> connection, MagmaSystemContext* ctx)
      : msd_drv_(std::move(msd_drv)),
        dev_(std::move(dev)),
        connection_(std::move(connection)),
        ctx_(ctx) {
    connection_->SetNotificationCallback(NotificationCallback, this);

    uint64_t buffer_size = sizeof(magma_command_buffer) + sizeof(uint64_t) * kSignalSemaphoreCount +
                           sizeof(magma_exec_resource) * kNumResources;

    buffer_ = magma::PlatformBuffer::Create(buffer_size, "command-buffer-backing");
    DASSERT(buffer_);

    DLOG("CommandBuffer backing buffer: %p", buffer_.get());

    bool success = buffer_->MapCpu(&buffer_data_);
    DASSERT(success);
    DASSERT(buffer_data_);

    abi_cmd_buf()->resource_count = kNumResources;
    abi_cmd_buf()->batch_buffer_resource_index = 0;
    abi_cmd_buf()->batch_start_offset = 0;
    abi_cmd_buf()->wait_semaphore_count = kWaitSemaphoreCount;
    abi_cmd_buf()->signal_semaphore_count = kSignalSemaphoreCount;

    // batch buffer
    {
      auto batch_buf = &abi_resources()[0];
      auto buffer = MagmaSystemBuffer::Create(
          magma::PlatformBuffer::Create(kBufferSize, "command-buffer-batch"));
      DASSERT(buffer);
      uint32_t duplicate_handle;
      success = buffer->platform_buffer()->duplicate_handle(&duplicate_handle);
      DASSERT(success);
      uint64_t id = buffer->platform_buffer()->id();
      success = connection_->ImportBuffer(duplicate_handle, id).ok();
      DASSERT(success);
      resources_.push_back(connection_->LookupBuffer(id).get());
      success = buffer->platform_buffer()->duplicate_handle(&duplicate_handle);
      DASSERT(success);
      batch_buf->buffer_id = id;
      batch_buf->offset = 0;
      batch_buf->length = buffer->platform_buffer()->size();
    }

    // other buffers
    for (uint32_t i = 1; i < kNumResources; i++) {
      auto resource = &abi_resources()[i];
      auto buffer =
          MagmaSystemBuffer::Create(magma::PlatformBuffer::Create(kBufferSize, "resource"));
      DASSERT(buffer);
      uint32_t duplicate_handle;
      success = buffer->platform_buffer()->duplicate_handle(&duplicate_handle);
      DASSERT(success);
      uint64_t id = buffer->platform_buffer()->id();
      success = connection_->ImportBuffer(duplicate_handle, id).ok();
      DASSERT(success);
      resources_.push_back(connection_->LookupBuffer(id).get());
      success = buffer->platform_buffer()->duplicate_handle(&duplicate_handle);
      DASSERT(success);
      resource->buffer_id = id;
      resource->offset = 0;
      resource->length = buffer->platform_buffer()->size();
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
      success =
          connection_
              ->ImportObject(duplicate_handle, magma::PlatformObject::SEMAPHORE, semaphore->id())
              .ok();
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
      success =
          connection_
              ->ImportObject(duplicate_handle, magma::PlatformObject::SEMAPHORE, semaphore->id())
              .ok();
      DASSERT(success);
      abi_signal_semaphore_ids()[i] = semaphore->id();
      msd_signal_semaphores_.push_back(
          connection_->LookupSemaphore(semaphore->id())->msd_semaphore());
    }
  }

  msd_driver_unique_ptr_t msd_drv_;
  std::shared_ptr<MagmaSystemDevice> dev_;
  std::unique_ptr<MagmaSystemConnection> connection_;
  MagmaSystemContext* ctx_;  // owned by the connection

  std::unique_ptr<magma::PlatformBuffer> buffer_;
  // mapped address of buffer_, do not free
  void* buffer_data_ = nullptr;

  std::vector<MagmaSystemBuffer*> resources_;
  std::vector<msd_buffer_t*> msd_resources_;

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores_;
  std::vector<msd_semaphore_t*> msd_wait_semaphores_;
  std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores_;
  std::vector<msd_semaphore_t*> msd_signal_semaphores_;
  std::vector<msd_notification_t> notifications_;
};
