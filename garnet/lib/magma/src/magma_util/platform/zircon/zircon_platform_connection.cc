// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/gpu/magma/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/task.h>
#include <lib/async/time.h>
#include <lib/async/wait.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/channel.h>
#include <lib/zx/profile.h>
#include <zircon/status.h>

#include "platform_connection.h"
#include "platform_handle.h"
#include "zircon_platform_event.h"

namespace magma {

class ZirconPlatformConnection : public PlatformConnection, public fuchsia::gpu::magma::Primary {
 public:
  struct AsyncWait : public async_wait {
    AsyncWait(ZirconPlatformConnection* connection, zx_handle_t object, zx_signals_t trigger) {
      this->state = ASYNC_STATE_INIT;
      this->handler = AsyncWaitHandlerStatic;
      this->object = object;
      this->trigger = trigger;
      this->options = 0;
      this->connection = connection;
    }
    ZirconPlatformConnection* connection;
  };

  struct AsyncTask : public async_task {
    AsyncTask(ZirconPlatformConnection* connection, msd_notification_t* notification) {
      this->state = ASYNC_STATE_INIT;
      this->handler = AsyncTaskHandlerStatic;
      this->deadline = async_now(connection->async_loop()->dispatcher());
      this->connection = connection;
      // Copy the notification struct
      this->notification = *notification;
    }

    ZirconPlatformConnection* connection;
    msd_notification_t notification;
  };

  ZirconPlatformConnection(std::unique_ptr<Delegate> delegate, msd_client_id_t client_id,
                           zx::channel server_endpoint, zx::channel client_endpoint,
                           zx::channel server_notification_endpoint,
                           zx::channel client_notification_endpoint,
                           std::shared_ptr<magma::PlatformEvent> shutdown_event,
                           std::unique_ptr<magma::PlatformHandle> thread_profile)
      : magma::PlatformConnection(shutdown_event, client_id, std::move(thread_profile)),
        delegate_(std::move(delegate)),
        client_endpoint_(std::move(client_endpoint)),
        server_notification_endpoint_(std::move(server_notification_endpoint)),
        client_notification_endpoint_(std::move(client_notification_endpoint)),
        async_loop_(&kAsyncLoopConfigNeverAttachToThread),
        async_wait_shutdown_(
            this, static_cast<magma::ZirconPlatformEvent*>(shutdown_event.get())->zx_handle(),
            ZX_EVENT_SIGNALED),
        binding_(this, std::move(server_endpoint), async_loop_.dispatcher()) {
    binding_.set_error_handler([this](zx_status_t status) { async_loop()->Quit(); });
    delegate_->SetNotificationCallback(NotificationCallbackStatic, this);
  }

  ~ZirconPlatformConnection() { delegate_->SetNotificationCallback(nullptr, 0); }

  bool HandleRequest() override {
    zx_status_t status = async_loop_.Run(zx::time::infinite(), true /* once */);
    if (status != ZX_OK)
      return false;
    return true;
  }

  bool BeginShutdownWait() {
    zx_status_t status = async_begin_wait(async_loop()->dispatcher(), &async_wait_shutdown_);
    if (status != ZX_OK)
      return DRETF(false, "Couldn't begin wait on shutdown: %s", zx_status_get_string(status));
    return true;
  }

  uint32_t GetClientEndpoint() override {
    DASSERT(client_endpoint_);
    return client_endpoint_.release();
  }

  uint32_t GetClientNotificationEndpoint() override {
    DASSERT(client_notification_endpoint_);
    return client_notification_endpoint_.release();
  }

  async::Loop* async_loop() { return &async_loop_; }

 private:
  static void AsyncWaitHandlerStatic(async_dispatcher_t* dispatcher, async_wait_t* async_wait,
                                     zx_status_t status, const zx_packet_signal_t* signal) {
    auto wait = static_cast<AsyncWait*>(async_wait);
    wait->connection->AsyncWaitHandler(dispatcher, wait, status, signal);
  }

  void AsyncWaitHandler(async_dispatcher_t* dispatcher, AsyncWait* wait, zx_status_t status,
                        const zx_packet_signal_t* signal) {
    if (status != ZX_OK)
      return;

    bool quit = false;
    if (wait == &async_wait_shutdown_) {
      DASSERT(signal->observed == ZX_EVENT_SIGNALED);
      quit = true;
      DLOG("got shutdown event");
    } else {
      DASSERT(false);
    }

    if (quit) {
      async_loop()->Quit();
    }
  }

  // Could occur on an arbitrary thread (see |msd_connection_set_notification_callback|).
  // MSD must ensure we aren't in the process of destroying our connection.
  static void NotificationCallbackStatic(void* token, msd_notification_t* notification) {
    auto connection = static_cast<ZirconPlatformConnection*>(token);
    zx_status_t status = async_post_task(connection->async_loop()->dispatcher(),
                                         new AsyncTask(connection, notification));
    if (status != ZX_OK)
      DLOG("async_post_task failed, status %s", zx_status_get_string(status));
  }

  static void AsyncTaskHandlerStatic(async_dispatcher_t* dispatcher, async_task_t* async_task,
                                     zx_status_t status) {
    auto task = static_cast<AsyncTask*>(async_task);
    task->connection->AsyncTaskHandler(dispatcher, task, status);
    delete task;
  }

  bool AsyncTaskHandler(async_dispatcher_t* dispatcher, AsyncTask* task, zx_status_t status) {
    switch (static_cast<MSD_CONNECTION_NOTIFICATION_TYPE>(task->notification.type)) {
      case MSD_CONNECTION_NOTIFICATION_CHANNEL_SEND: {
        zx_status_t status = zx_channel_write(server_notification_endpoint_.get(), 0,
                                              task->notification.u.channel_send.data,
                                              task->notification.u.channel_send.size, nullptr, 0);
        if (status != ZX_OK)
          return DRETF(false, "Failed writing to channel: %s", zx_status_get_string(status));
        return true;
      }
      case MSD_CONNECTION_NOTIFICATION_CONTEXT_KILLED:
        // Kill the connection.
        ShutdownEvent()->Signal();
        return true;
    }
    return DRETF(false, "Unhandled notification type: %d", task->notification.type);
  }

  void ImportBuffer(zx::vmo vmo) override {
    DLOG("ZirconPlatformConnection - ImportBuffer");
    uint64_t buffer_id;
    if (!delegate_->ImportBuffer(vmo.release(), &buffer_id)) {
      SetError(MAGMA_STATUS_INVALID_ARGS);
    }
  }

  void ReleaseBuffer(uint64_t buffer_id) override {
    DLOG("ZirconPlatformConnection: ReleaseBuffer");
    if (!delegate_->ReleaseBuffer(buffer_id))
      SetError(MAGMA_STATUS_INVALID_ARGS);
  }

  void ImportObject(zx::handle handle, uint32_t object_type) override {
    DLOG("ZirconPlatformConnection: ImportObject");
    if (!delegate_->ImportObject(handle.release(), static_cast<PlatformObject::Type>(object_type)))
      SetError(MAGMA_STATUS_INVALID_ARGS);
  }

  void ReleaseObject(uint64_t object_id, uint32_t object_type) override {
    DLOG("ZirconPlatformConnection: ReleaseObject");
    if (!delegate_->ReleaseObject(object_id, static_cast<PlatformObject::Type>(object_type)))
      SetError(MAGMA_STATUS_INVALID_ARGS);
  }

  void CreateContext(uint32_t context_id) override {
    DLOG("ZirconPlatformConnection: CreateContext");
    if (!delegate_->CreateContext(context_id))
      SetError(MAGMA_STATUS_INTERNAL_ERROR);
  }

  void DestroyContext(uint32_t context_id) override {
    DLOG("ZirconPlatformConnection: DestroyContext");
    if (!delegate_->DestroyContext(context_id))
      SetError(MAGMA_STATUS_INTERNAL_ERROR);
  }

  void ExecuteCommandBufferWithResources(uint32_t context_id,
                                         fuchsia::gpu::magma::CommandBuffer fidl_command_buffer,
                                         std::vector<fuchsia::gpu::magma::Resource> fidl_resources,
                                         std::vector<uint64_t> wait_semaphores,
                                         std::vector<uint64_t> signal_semaphores) override {
    auto command_buffer = std::make_unique<magma_system_command_buffer>();
    *command_buffer = {
        .batch_buffer_resource_index = fidl_command_buffer.batch_buffer_resource_index,
        .batch_start_offset = fidl_command_buffer.batch_start_offset,
        .num_resources = static_cast<uint32_t>(fidl_resources.size()),
        .wait_semaphore_count = static_cast<uint32_t>(wait_semaphores.size()),
        .signal_semaphore_count = static_cast<uint32_t>(signal_semaphores.size()),
    };

    std::vector<magma_system_exec_resource> resources;
    resources.reserve(fidl_resources.size());

    for (auto& resource : fidl_resources) {
      resources.push_back({
          resource.buffer,
          resource.offset,
          resource.length,
      });
    }

    // Merge semaphores into one vector
    wait_semaphores.insert(wait_semaphores.end(), signal_semaphores.begin(),
                           signal_semaphores.end());

    magma::Status status = delegate_->ExecuteCommandBufferWithResources(
        context_id, std::move(command_buffer), std::move(resources), std::move(wait_semaphores));

    if (!status)
      SetError(status.get());
  }

  void ExecuteImmediateCommands(uint32_t context_id, ::std::vector<uint8_t> command_data_vec,
                                ::std::vector<uint64_t> semaphore_vec) override {
    DLOG("ZirconPlatformConnection: ExecuteImmediateCommands");
    magma::Status status = delegate_->ExecuteImmediateCommands(
        context_id, command_data_vec.size(), command_data_vec.data(), semaphore_vec.size(),
        semaphore_vec.data());
    if (!status)
      SetError(status.get());
  }

  void GetError(fuchsia::gpu::magma::Primary::GetErrorCallback error_callback) override {
    magma_status_t result = error_;
    error_ = 0;
    error_callback(result);
  }

  void MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset, uint64_t page_count,
                    uint64_t flags) override {
    DLOG("ZirconPlatformConnection: MapBufferGpuFIDL");
    if (!delegate_->MapBufferGpu(buffer_id, gpu_va, page_offset, page_count, flags))
      SetError(MAGMA_STATUS_INVALID_ARGS);
  }

  void UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va) override {
    DLOG("ZirconPlatformConnection: UnmapBufferGpuFIDL");
    if (!delegate_->UnmapBufferGpu(buffer_id, gpu_va))
      SetError(MAGMA_STATUS_INVALID_ARGS);
  }

  void CommitBuffer(uint64_t buffer_id, uint64_t page_offset, uint64_t page_count) override {
    DLOG("ZirconPlatformConnection: CommitBufferFIDL");
    if (!delegate_->CommitBuffer(buffer_id, page_offset, page_count))
      SetError(MAGMA_STATUS_INVALID_ARGS);
  }

  void SetError(magma_status_t error) {
    if (!error_)
      error_ = DRET_MSG(error, "ZirconPlatformConnection encountered dispatcher error");
  }

  bool WriteError(magma_status_t error) {
    DLOG("Writing error %d to channel", error);
    zx_status_t status = server_endpoint_.write(0, &error, sizeof(error), nullptr, 0);
    return DRETF(status == ZX_OK, "failed to write to channel");
  }

  std::unique_ptr<Delegate> delegate_;
  zx::channel server_endpoint_;
  zx::channel client_endpoint_;
  magma_status_t error_{};
  zx::channel server_notification_endpoint_;
  zx::channel client_notification_endpoint_;
  async::Loop async_loop_;
  AsyncWait async_wait_shutdown_;

  fidl::Binding<fuchsia::gpu::magma::Primary> binding_;
};

std::shared_ptr<PlatformConnection> PlatformConnection::Create(
    std::unique_ptr<PlatformConnection::Delegate> delegate, msd_client_id_t client_id,
    std::unique_ptr<magma::PlatformHandle> thread_profile) {
  if (!delegate)
    return DRETP(nullptr, "attempting to create PlatformConnection with null delegate");

  zx::channel server_endpoint;
  zx::channel client_endpoint;
  zx_status_t status = zx::channel::create(0, &server_endpoint, &client_endpoint);
  if (status != ZX_OK)
    return DRETP(nullptr, "zx::channel::create failed");

  zx::channel server_notification_endpoint;
  zx::channel client_notification_endpoint;
  status = zx::channel::create(0, &server_notification_endpoint, &client_notification_endpoint);
  if (status != ZX_OK)
    return DRETP(nullptr, "zx::channel::create failed");

  auto shutdown_event = magma::PlatformEvent::Create();
  if (!shutdown_event)
    return DRETP(nullptr, "Failed to create shutdown event");

  auto connection = std::make_shared<ZirconPlatformConnection>(
      std::move(delegate), client_id, std::move(server_endpoint), std::move(client_endpoint),
      std::move(server_notification_endpoint), std::move(client_notification_endpoint),
      std::shared_ptr<magma::PlatformEvent>(std::move(shutdown_event)), std::move(thread_profile));

  if (!connection->BeginShutdownWait())
    return DRETP(nullptr, "Failed to begin shutdown wait");

  return connection;
}

}  // namespace magma
