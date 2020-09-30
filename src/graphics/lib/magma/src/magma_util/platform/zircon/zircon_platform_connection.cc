// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_connection.h"

#include <lib/fidl-async/cpp/bind.h>

namespace magma {

class ZirconPlatformPerfCountPool : public PlatformPerfCountPool {
 public:
  ZirconPlatformPerfCountPool(uint64_t id, zx::channel channel)
      : pool_id_(id), channel_(std::move(channel)) {}

  uint64_t pool_id() override { return pool_id_; }

  // Sends a OnPerformanceCounterReadCompleted. May be called from any thread.
  magma::Status SendPerformanceCounterCompletion(uint32_t trigger_id, uint64_t buffer_id,
                                                 uint32_t buffer_offset, uint64_t time,
                                                 uint32_t result_flags) override {
    zx_status_t status = llcpp::fuchsia::gpu::magma::PerformanceCounterEvents::
        SendOnPerformanceCounterReadCompletedEvent(
            zx::unowned_channel(channel_), trigger_id, buffer_id, buffer_offset, time,
            llcpp::fuchsia::gpu::magma::ResultFlags::TruncatingUnknown(result_flags));
    switch (status) {
      case ZX_OK:
        return MAGMA_STATUS_OK;
      case ZX_ERR_PEER_CLOSED:
        return MAGMA_STATUS_CONNECTION_LOST;
      case ZX_ERR_TIMED_OUT:
        return MAGMA_STATUS_TIMED_OUT;
      default:
        return MAGMA_STATUS_INTERNAL_ERROR;
    }
  }

 private:
  uint64_t pool_id_;
  zx::channel channel_;
};

bool ZirconPlatformConnection::Bind(zx::channel server_endpoint) {
  fidl::OnChannelClosedFn<llcpp::fuchsia::gpu::magma::Primary::Interface> channel_closed_callback =
      [](llcpp::fuchsia::gpu::magma::Primary::Interface* interface) {
        static_cast<ZirconPlatformConnection*>(interface)->async_loop()->Quit();
      };

  // TODO(fxbug.dev/48671): don't store an unowned handle
  server_endpoint_unowned_ = server_endpoint.get();

  llcpp::fuchsia::gpu::magma::Primary::Interface* interface = this;
  zx_status_t status =
      fidl::BindSingleInFlightOnly(async_loop()->dispatcher(), std::move(server_endpoint),
                                   interface, std::move(channel_closed_callback));

  if (status != ZX_OK)
    return DRETF(false, "fidl::BindSingleInFlightOnly failed: %d", status);

  return true;
}

bool ZirconPlatformConnection::HandleRequest() {
  zx_status_t status = async_loop_.Run(zx::time::infinite(), true /* once */);
  if (status != ZX_OK)
    return false;
  return true;
}

bool ZirconPlatformConnection::BeginShutdownWait() {
  zx_status_t status = async_begin_wait(async_loop()->dispatcher(), &async_wait_shutdown_);
  if (status != ZX_OK)
    return DRETF(false, "Couldn't begin wait on shutdown: %s", zx_status_get_string(status));
  return true;
}

void ZirconPlatformConnection::AsyncWaitHandler(async_dispatcher_t* dispatcher, AsyncWait* wait,
                                                zx_status_t status,
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

bool ZirconPlatformConnection::AsyncTaskHandler(async_dispatcher_t* dispatcher, AsyncTask* task,
                                                zx_status_t status) {
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
    case MSD_CONNECTION_NOTIFICATION_PERFORMANCE_COUNTERS_READ_COMPLETED:
      // Should be handled in MagmaSystemConnection.
      break;
  }
  return DRETF(false, "Unhandled notification type: %d", task->notification.type);
}

void ZirconPlatformConnection::EnableFlowControl(EnableFlowControlCompleter::Sync _completer) {
  flow_control_enabled_ = true;
}

void ZirconPlatformConnection::FlowControl(uint64_t size) {
  if (!flow_control_enabled_)
    return;

  messages_consumed_ += 1;
  bytes_imported_ += size;

  if (messages_consumed_ >= kMaxInflightMessages / 2) {
    zx_status_t status = llcpp::fuchsia::gpu::magma::Primary::SendOnNotifyMessagesConsumedEvent(
        zx::unowned_channel(server_endpoint_unowned_), messages_consumed_);
    if (status == ZX_OK) {
      messages_consumed_ = 0;
    } else if (status != ZX_ERR_PEER_CLOSED) {
      DMESSAGE("SendOnNotifyMessagesConsumedEvent failed: %d", status);
    }
  }

  if (bytes_imported_ >= kMaxInflightBytes / 2) {
    zx_status_t status = llcpp::fuchsia::gpu::magma::Primary::SendOnNotifyMemoryImportedEvent(
        zx::unowned_channel(server_endpoint_unowned_), bytes_imported_);
    if (status == ZX_OK) {
      bytes_imported_ = 0;
    } else if (status != ZX_ERR_PEER_CLOSED) {
      DMESSAGE("SendOnNotifyMemoryImportedEvent failed: %d", status);
    }
  }
}

void ZirconPlatformConnection::ImportBuffer(::zx::vmo buffer,
                                            ImportBufferCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection - ImportBuffer");
  uint64_t size = 0;
  buffer.get_size(&size);
  FlowControl(size);

  uint64_t buffer_id;
  if (!delegate_->ImportBuffer(buffer.release(), &buffer_id))
    SetError(MAGMA_STATUS_INVALID_ARGS);
}

void ZirconPlatformConnection::ReleaseBuffer(uint64_t buffer_id,
                                             ReleaseBufferCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: ReleaseBuffer");
  FlowControl();

  if (!delegate_->ReleaseBuffer(buffer_id))
    SetError(MAGMA_STATUS_INVALID_ARGS);
}

void ZirconPlatformConnection::ImportObject(zx::handle handle, uint32_t object_type,
                                            ImportObjectCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: ImportObject");
  FlowControl();

  if (!delegate_->ImportObject(handle.release(), static_cast<PlatformObject::Type>(object_type)))
    SetError(MAGMA_STATUS_INVALID_ARGS);
}

void ZirconPlatformConnection::ReleaseObject(uint64_t object_id, uint32_t object_type,
                                             ReleaseObjectCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: ReleaseObject");
  FlowControl();

  if (!delegate_->ReleaseObject(object_id, static_cast<PlatformObject::Type>(object_type)))
    SetError(MAGMA_STATUS_INVALID_ARGS);
}

void ZirconPlatformConnection::CreateContext(uint32_t context_id,
                                             CreateContextCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: CreateContext");
  FlowControl();

  if (!delegate_->CreateContext(context_id))
    SetError(MAGMA_STATUS_INTERNAL_ERROR);
}

void ZirconPlatformConnection::DestroyContext(uint32_t context_id,
                                              DestroyContextCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: DestroyContext");
  FlowControl();

  if (!delegate_->DestroyContext(context_id))
    SetError(MAGMA_STATUS_INTERNAL_ERROR);
}

void ZirconPlatformConnection::ExecuteCommandBufferWithResources(
    uint32_t context_id, llcpp::fuchsia::gpu::magma::CommandBuffer fidl_command_buffer,
    ::fidl::VectorView<llcpp::fuchsia::gpu::magma::Resource> fidl_resources,
    ::fidl::VectorView<uint64_t> wait_semaphores, ::fidl::VectorView<uint64_t> signal_semaphores,
    ExecuteCommandBufferWithResourcesCompleter::Sync _completer) {
  FlowControl();

  auto command_buffer = std::make_unique<magma_system_command_buffer>();

  *command_buffer = {
      .resource_count = static_cast<uint32_t>(fidl_resources.count()),
      .batch_buffer_resource_index = fidl_command_buffer.batch_buffer_resource_index,
      .batch_start_offset = fidl_command_buffer.batch_start_offset,
      .wait_semaphore_count = static_cast<uint32_t>(wait_semaphores.count()),
      .signal_semaphore_count = static_cast<uint32_t>(signal_semaphores.count()),
  };

  std::vector<magma_system_exec_resource> resources;
  resources.reserve(fidl_resources.count());

  for (auto& resource : fidl_resources) {
    resources.push_back({
        resource.buffer,
        resource.offset,
        resource.length,
    });
  }

  // Merge semaphores into one vector
  std::vector<uint64_t> semaphores;
  semaphores.reserve(wait_semaphores.count() + signal_semaphores.count());

  for (uint64_t semaphore_id : wait_semaphores) {
    semaphores.push_back(semaphore_id);
  }
  for (uint64_t semaphore_id : signal_semaphores) {
    semaphores.push_back(semaphore_id);
  }

  magma::Status status = delegate_->ExecuteCommandBufferWithResources(
      context_id, std::move(command_buffer), std::move(resources), std::move(semaphores));

  if (!status)
    SetError(status.get());
}

void ZirconPlatformConnection::ExecuteImmediateCommands(
    uint32_t context_id, ::fidl::VectorView<uint8_t> command_data_vec,
    ::fidl::VectorView<uint64_t> semaphore_vec,
    ExecuteImmediateCommandsCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: ExecuteImmediateCommands");
  FlowControl();

  magma::Status status = delegate_->ExecuteImmediateCommands(
      context_id, command_data_vec.count(), command_data_vec.mutable_data(), semaphore_vec.count(),
      semaphore_vec.mutable_data());
  if (!status)
    SetError(status.get());
}

void ZirconPlatformConnection::GetError(GetErrorCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: GetError");
  magma_status_t result = error_;
  error_ = 0;
  _completer.Reply(result);
}

void ZirconPlatformConnection::MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va,
                                            uint64_t page_offset, uint64_t page_count,
                                            uint64_t flags,
                                            MapBufferGpuCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: MapBufferGpuFIDL");
  FlowControl();

  if (!delegate_->MapBufferGpu(buffer_id, gpu_va, page_offset, page_count, flags))
    SetError(MAGMA_STATUS_INVALID_ARGS);
}

void ZirconPlatformConnection::UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va,
                                              UnmapBufferGpuCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: UnmapBufferGpuFIDL");
  FlowControl();

  if (!delegate_->UnmapBufferGpu(buffer_id, gpu_va))
    SetError(MAGMA_STATUS_INVALID_ARGS);
}

void ZirconPlatformConnection::CommitBuffer(uint64_t buffer_id, uint64_t page_offset,
                                            uint64_t page_count,
                                            CommitBufferCompleter::Sync _completer) {
  DLOG("ZirconPlatformConnection: CommitBufferFIDL");
  FlowControl();

  if (!delegate_->CommitBuffer(buffer_id, page_offset, page_count))
    SetError(MAGMA_STATUS_INVALID_ARGS);
}

void ZirconPlatformConnection::AccessPerformanceCounters(
    zx::event event, AccessPerformanceCountersCompleter::Sync completer) {
  DLOG("ZirconPlatformConnection:::AccessPerformanceCounters");
  FlowControl();

  magma::Status status =
      delegate_->AccessPerformanceCounters(magma::PlatformHandle::Create(event.release()));
  if (!status) {
    SetError(status.get());
  }
}

void ZirconPlatformConnection::IsPerformanceCounterAccessEnabled(
    IsPerformanceCounterAccessEnabledCompleter::Sync completer) {
  DLOG("ZirconPlatformConnection:::IsPerformanceCounterAccessEnabled");
  completer.Reply(delegate_->IsPerformanceCounterAccessEnabled());
}

void ZirconPlatformConnection::EnablePerformanceCounters(
    ::fidl::VectorView<uint64_t> counters, EnablePerformanceCountersCompleter::Sync completer) {
  FlowControl();
  magma::Status status = delegate_->EnablePerformanceCounters(counters.data(), counters.count());
  if (!status) {
    SetError(status.get());
  }
}

void ZirconPlatformConnection::CreatePerformanceCounterBufferPool(
    uint64_t pool_id, zx::channel event_channel,
    CreatePerformanceCounterBufferPoolCompleter::Sync completer) {
  FlowControl();
  auto pool = std::make_unique<ZirconPlatformPerfCountPool>(pool_id, std::move(event_channel));
  magma::Status status = delegate_->CreatePerformanceCounterBufferPool(std::move(pool));
  if (!status) {
    SetError(status.get());
  }
}

void ZirconPlatformConnection::ReleasePerformanceCounterBufferPool(
    uint64_t pool_id, ReleasePerformanceCounterBufferPoolCompleter::Sync completer) {
  FlowControl();
  magma::Status status = delegate_->ReleasePerformanceCounterBufferPool(pool_id);
  if (!status) {
    SetError(status.get());
  }
}

void ZirconPlatformConnection::AddPerformanceCounterBufferOffsetsToPool(
    uint64_t pool_id, fidl::VectorView<llcpp::fuchsia::gpu::magma::BufferOffset> offsets,
    AddPerformanceCounterBufferOffsetsToPoolCompleter::Sync completer) {
  FlowControl();
  for (auto& offset : offsets) {
    magma::Status status = delegate_->AddPerformanceCounterBufferOffsetToPool(
        pool_id, offset.buffer_id, offset.offset, offset.size);
    if (!status) {
      SetError(status.get());
    }
  }
}

void ZirconPlatformConnection::RemovePerformanceCounterBufferFromPool(
    uint64_t pool_id, uint64_t buffer_id,
    RemovePerformanceCounterBufferFromPoolCompleter::Sync completer) {
  FlowControl();
  magma::Status status = delegate_->RemovePerformanceCounterBufferFromPool(pool_id, buffer_id);
  if (!status) {
    SetError(status.get());
  }
}

void ZirconPlatformConnection::DumpPerformanceCounters(
    uint64_t pool_id, uint32_t trigger_id, DumpPerformanceCountersCompleter::Sync completer) {
  FlowControl();
  magma::Status status = delegate_->DumpPerformanceCounters(pool_id, trigger_id);
  if (!status) {
    SetError(status.get());
  }
}

void ZirconPlatformConnection::ClearPerformanceCounters(
    ::fidl::VectorView<uint64_t> counters, ClearPerformanceCountersCompleter::Sync completer) {
  FlowControl();
  magma::Status status = delegate_->ClearPerformanceCounters(counters.data(), counters.count());
  if (!status) {
    SetError(status.get());
  }
}

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
      std::move(delegate), client_id, std::move(client_endpoint),
      std::move(server_notification_endpoint), std::move(client_notification_endpoint),
      std::shared_ptr<magma::PlatformEvent>(std::move(shutdown_event)), std::move(thread_profile));

  if (!connection->Bind(std::move(server_endpoint)))
    return DRETP(nullptr, "fidl::BindSingleInFlightOnly failed: %d", status);

  if (!connection->BeginShutdownWait())
    return DRETP(nullptr, "Failed to begin shutdown wait");

  return connection;
}

}  // namespace magma
