// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PLATFORM_CONNECTION_H
#define ZIRCON_PLATFORM_CONNECTION_H

#include <fuchsia/gpu/magma/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/task.h>
#include <lib/async/time.h>
#include <lib/async/wait.h>
#include <lib/zx/channel.h>
#include <lib/zx/profile.h>
#include <zircon/status.h>

#include "platform_connection.h"
#include "platform_handle.h"
#include "zircon_platform_event.h"

namespace magma {

class ZirconPlatformConnection : public llcpp::fuchsia::gpu::magma::Primary::Interface,
                                 public PlatformConnection {
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
                           zx::channel client_endpoint, zx::channel server_notification_endpoint,
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
            ZX_EVENT_SIGNALED) {
    delegate_->SetNotificationCallback(NotificationCallbackStatic, this);
  }

  ~ZirconPlatformConnection() { delegate_->SetNotificationCallback(nullptr, 0); }

  uint32_t GetClientEndpoint() override {
    DASSERT(client_endpoint_);
    return client_endpoint_.release();
  }

  uint32_t GetClientNotificationEndpoint() override {
    DASSERT(client_notification_endpoint_);
    return client_notification_endpoint_.release();
  }

  bool Bind(zx::channel server_endpoint);

  bool HandleRequest() override;

  bool BeginShutdownWait();

  async::Loop* async_loop() { return &async_loop_; }

 private:
  static void AsyncWaitHandlerStatic(async_dispatcher_t* dispatcher, async_wait_t* async_wait,
                                     zx_status_t status, const zx_packet_signal_t* signal) {
    auto wait = static_cast<AsyncWait*>(async_wait);
    wait->connection->AsyncWaitHandler(dispatcher, wait, status, signal);
  }

  void AsyncWaitHandler(async_dispatcher_t* dispatcher, AsyncWait* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);

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

  bool AsyncTaskHandler(async_dispatcher_t* dispatcher, AsyncTask* task, zx_status_t status);

  void ImportBuffer(::zx::vmo buffer, ImportBufferCompleter::Sync& _completer) override;
  void ReleaseBuffer(uint64_t buffer_id, ReleaseBufferCompleter::Sync& _completer) override;
  void ImportObject(zx::handle handle, uint32_t object_type,
                    ImportObjectCompleter::Sync& _completer) override;
  void ReleaseObject(uint64_t object_id, uint32_t object_type,
                     ReleaseObjectCompleter::Sync& _completer) override;
  void CreateContext(uint32_t context_id, CreateContextCompleter::Sync& _completer) override;
  void DestroyContext(uint32_t context_id, DestroyContextCompleter::Sync& _completer) override;
  void ExecuteCommandBufferWithResources(
      uint32_t context_id, llcpp::fuchsia::gpu::magma::CommandBuffer fidl_command_buffer,
      ::fidl::VectorView<llcpp::fuchsia::gpu::magma::Resource> fidl_resources,
      ::fidl::VectorView<uint64_t> wait_semaphores, ::fidl::VectorView<uint64_t> signal_semaphores,
      ExecuteCommandBufferWithResourcesCompleter::Sync& _completer) override;
  void ExecuteImmediateCommands(uint32_t context_id, ::fidl::VectorView<uint8_t> command_data_vec,
                                ::fidl::VectorView<uint64_t> semaphore_vec,
                                ExecuteImmediateCommandsCompleter::Sync& _completer) override;
  void GetError(GetErrorCompleter::Sync& _completer) override;
  void MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset, uint64_t page_count,
                    uint64_t flags, MapBufferGpuCompleter::Sync& _completer) override;
  void UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va,
                      UnmapBufferGpuCompleter::Sync& _completer) override;
  void CommitBuffer(uint64_t buffer_id, uint64_t page_offset, uint64_t page_count,
                    CommitBufferCompleter::Sync& _completer) override;
  void AccessPerformanceCounters(zx::event event,
                                 AccessPerformanceCountersCompleter::Sync& completer) override;
  void IsPerformanceCounterAccessEnabled(
      IsPerformanceCounterAccessEnabledCompleter::Sync& completer) override;

  void EnableFlowControl(EnableFlowControlCompleter::Sync& _completer) override;

  std::pair<uint64_t, uint64_t> GetFlowControlCounts() override {
    return {messages_consumed_, bytes_imported_};
  }

  void EnablePerformanceCounters(::fidl::VectorView<uint64_t> counters,
                                 EnablePerformanceCountersCompleter::Sync& completer) override;
  void CreatePerformanceCounterBufferPool(
      uint64_t pool_id, zx::channel event_channel,
      CreatePerformanceCounterBufferPoolCompleter::Sync& completer) override;
  void ReleasePerformanceCounterBufferPool(
      uint64_t pool_id, ReleasePerformanceCounterBufferPoolCompleter::Sync& completer) override;
  void AddPerformanceCounterBufferOffsetsToPool(
      uint64_t pool_id, fidl::VectorView<llcpp::fuchsia::gpu::magma::BufferOffset> offsets,
      AddPerformanceCounterBufferOffsetsToPoolCompleter::Sync& completer) override;
  void RemovePerformanceCounterBufferFromPool(
      uint64_t pool_id, uint64_t buffer_id,
      RemovePerformanceCounterBufferFromPoolCompleter::Sync& completer) override;
  void DumpPerformanceCounters(uint64_t pool_id, uint32_t trigger_id,
                               DumpPerformanceCountersCompleter::Sync& completer) override;
  void ClearPerformanceCounters(::fidl::VectorView<uint64_t> counters,
                                ClearPerformanceCountersCompleter::Sync& completer) override;

  void SetError(magma_status_t error) {
    if (!error_)
      error_ = DRET_MSG(error, "ZirconPlatformConnection encountered dispatcher error");
  }
  void FlowControl(uint64_t size = 0);

  std::unique_ptr<Delegate> delegate_;
  zx::channel client_endpoint_;
  magma_status_t error_{};
  zx::channel server_notification_endpoint_;
  zx::channel client_notification_endpoint_;
  zx::channel performance_counter_event_channel_;
  async::Loop async_loop_;
  AsyncWait async_wait_shutdown_;

  // Flow control
  bool flow_control_enabled_ = false;
  zx_handle_t server_endpoint_unowned_;
  uint64_t messages_consumed_ = 0;
  uint64_t bytes_imported_ = 0;

  friend class FlowControlChecker;
};

}  // namespace magma

#endif  // ZIRCON_PLATFORM_CONNECTION_H
