// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_connection.h"

#include <optional>

#include "magma_common_defs.h"
#include "zircon_platform_status.h"

namespace {
std::optional<magma::PlatformObject::Type> GetObjectType(fuchsia_gpu_magma::ObjectType fidl_type) {
  switch (fidl_type) {
    case fuchsia_gpu_magma::ObjectType::kBuffer:
      return magma::PlatformObject::Type::BUFFER;
    case fuchsia_gpu_magma::ObjectType::kEvent:
      return magma::PlatformObject::Type::SEMAPHORE;
    default:
      return std::nullopt;
  }
}

std::optional<int> GetBufferOp(fuchsia_gpu_magma::BufferOp fidl_type) {
  switch (fidl_type) {
    case fuchsia_gpu_magma::wire::BufferOp::kPopulateTables:
      return MAGMA_BUFFER_RANGE_OP_POPULATE_TABLES;
    case fuchsia_gpu_magma::wire::BufferOp::kDepopulateTables:
      return MAGMA_BUFFER_RANGE_OP_DEPOPULATE_TABLES;
    default:
      return std::nullopt;
  }
}

}  // namespace

namespace magma {

class ZirconPlatformPerfCountPool : public PlatformPerfCountPool {
 public:
  ZirconPlatformPerfCountPool(uint64_t id, zx::channel channel)
      : pool_id_(id), server_end_(std::move(channel)) {}

  uint64_t pool_id() override { return pool_id_; }

  // Sends a OnPerformanceCounterReadCompleted. May be called from any thread.
  magma::Status SendPerformanceCounterCompletion(uint32_t trigger_id, uint64_t buffer_id,
                                                 uint32_t buffer_offset, uint64_t time,
                                                 uint32_t result_flags) override {
    fidl::Arena allocator;
    auto builder = fuchsia_gpu_magma::wire::
        PerformanceCounterEventsOnPerformanceCounterReadCompletedRequest::Builder(allocator);
    builder.trigger_id(trigger_id)
        .buffer_id(buffer_id)
        .buffer_offset(buffer_offset)
        .timestamp(time)
        .flags(fuchsia_gpu_magma::wire::ResultFlags::TruncatingUnknown(result_flags));

    fidl::Status result =
        fidl::WireSendEvent(server_end_)->OnPerformanceCounterReadCompleted(builder.Build());
    switch (result.status()) {
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
  fidl::ServerEnd<fuchsia_gpu_magma::PerformanceCounterEvents> server_end_;
};

void ZirconPlatformConnection::SetError(fidl::CompleterBase* completer, magma_status_t error) {
  if (!error_) {
    error_ = DRET_MSG(error, "ZirconPlatformConnection encountered dispatcher error");
    if (completer) {
      completer->Close(magma::ToZxStatus(error));
    } else {
      server_binding_->Close(magma::ToZxStatus(error));
    }
    async_loop()->Quit();
  }
}

bool ZirconPlatformConnection::Bind(zx::channel server_endpoint) {
  fidl::OnUnboundFn<ZirconPlatformConnection> unbind_callback =
      [](ZirconPlatformConnection* self, fidl::UnbindInfo unbind_info,
         fidl::ServerEnd<fuchsia_gpu_magma::Primary> server_channel) {
        // |kDispatcherError| indicates the async loop itself is shutting down,
        // which could only happen when |interface| is being destructed.
        // Therefore, we must avoid using the same object.
        if (unbind_info.reason() == fidl::Reason::kDispatcherError)
          return;

        self->server_binding_ = cpp17::nullopt;
        self->async_loop()->Quit();
      };

  // Note: the async loop should not be started until we assign |server_binding_|.
  server_binding_ =
      fidl::BindServer(async_loop()->dispatcher(),
                       fidl::ServerEnd<fuchsia_gpu_magma::Primary>(std::move(server_endpoint)),
                       this, std::move(unbind_callback));
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
    server_binding_->Close(ZX_ERR_CANCELED);
    async_loop()->Quit();
  }
}

struct AsyncHandleWait : public async_wait {
  AsyncHandleWait(msd_connection_handle_wait_complete_t completer, void* completer_context,
                  zx_handle_t object) {
    this->state = ASYNC_STATE_INIT;
    this->handler = Handler;
    this->object = object;
    this->trigger = ZX_EVENT_SIGNALED;
    this->options = 0;
    this->completer = completer;
    this->completer_context = completer_context;
  }

  static void Handler(async_dispatcher_t* dispatcher, async_wait_t* async_wait, zx_status_t status,
                      const zx_packet_signal_t* signal) {
    auto wait = static_cast<AsyncHandleWait*>(async_wait);

    wait->completer(wait->completer_context, magma::FromZxStatus(status).get(), wait->object);

    delete wait;
  }

  msd_connection_handle_wait_complete_t completer;
  void* completer_context;
};

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
      // Setting the error will close the connection.
      SetError(nullptr, MAGMA_STATUS_CONTEXT_KILLED);
      return true;

    case MSD_CONNECTION_NOTIFICATION_PERFORMANCE_COUNTERS_READ_COMPLETED:
      // Should be handled in MagmaSystemConnection.
      break;

    case MSD_CONNECTION_NOTIFICATION_HANDLE_WAIT: {
      DASSERT(task->notification.u.handle_wait.handle != ZX_HANDLE_INVALID);

      auto wait_ptr = std::make_unique<AsyncHandleWait>(
          task->notification.u.handle_wait.completer, task->notification.u.handle_wait.wait_context,
          task->notification.u.handle_wait.handle);

      zx_status_t status = async_begin_wait(async_loop()->dispatcher(), wait_ptr.get());
      if (status != ZX_OK)
        return DRETF(false, "async_begin_wait failed: %s", zx_status_get_string(status));

      task->notification.u.handle_wait.starter(task->notification.u.handle_wait.wait_context,
                                               wait_ptr.release());
      return true;
    }

    case MSD_CONNECTION_NOTIFICATION_HANDLE_WAIT_CANCEL: {
      auto wait_ptr =
          reinterpret_cast<AsyncHandleWait*>(task->notification.u.handle_wait_cancel.cancel_token);
      DASSERT(wait_ptr);

      zx_status_t status = async_cancel_wait(async_loop()->dispatcher(), wait_ptr);
      if (status != ZX_OK)
        return DRETF(false, "async_cancel_wait failed: %s", zx_status_get_string(status));

      // Call back to ensure cleanup
      AsyncHandleWait::Handler(async_loop()->dispatcher(), wait_ptr, ZX_ERR_CANCELED, nullptr);
      return true;
    }
  }
  return DRETF(false, "Unhandled notification type: %lu", task->notification.type);
}

void ZirconPlatformConnection::EnableFlowControl(EnableFlowControlCompleter::Sync& completer) {
  flow_control_enabled_ = true;
}

void ZirconPlatformConnection::FlowControl(uint64_t size) {
  if (!flow_control_enabled_)
    return;

  messages_consumed_ += 1;
  bytes_imported_ += size;

  if (messages_consumed_ >= kMaxInflightMessages / 2) {
    fidl::Status result =
        fidl::WireSendEvent(server_binding_.value())->OnNotifyMessagesConsumed(messages_consumed_);
    if (result.ok()) {
      messages_consumed_ = 0;
    } else if (!result.is_canceled() && !result.is_peer_closed()) {
      DMESSAGE("SendOnNotifyMessagesConsumedEvent failed: %s", result.FormatDescription().c_str());
    }
  }

  if (bytes_imported_ >= kMaxInflightBytes / 2) {
    fidl::Status result =
        fidl::WireSendEvent(server_binding_.value())->OnNotifyMemoryImported(bytes_imported_);
    if (result.ok()) {
      bytes_imported_ = 0;
    } else if (!result.is_canceled() && !result.is_peer_closed()) {
      DMESSAGE("SendOnNotifyMemoryImportedEvent failed: %s", result.FormatDescription().c_str());
    }
  }
}

void ZirconPlatformConnection::ImportObject2(ImportObject2RequestView request,
                                             ImportObject2Completer::Sync& completer) {
  DLOG("ZirconPlatformConnection: ImportObject2");

  auto object_type = GetObjectType(request->object_type);
  if (!object_type) {
    SetError(&completer, MAGMA_STATUS_INVALID_ARGS);
    return;
  }

  uint64_t size = 0;

  if (object_type == magma::PlatformObject::BUFFER) {
    zx::unowned_vmo vmo(request->object.get());
    zx_status_t status = vmo->get_size(&size);
    if (status != ZX_OK) {
      SetError(&completer, MAGMA_STATUS_INVALID_ARGS);
      return;
    }
  }
  FlowControl(size);

  if (!delegate_->ImportObject(request->object.release(), *object_type, request->object_id))
    SetError(&completer, MAGMA_STATUS_INVALID_ARGS);
}

void ZirconPlatformConnection::ReleaseObject(ReleaseObjectRequestView request,
                                             ReleaseObjectCompleter::Sync& completer) {
  DLOG("ZirconPlatformConnection: ReleaseObject");
  FlowControl();

  auto object_type = GetObjectType(request->object_type);
  if (!object_type) {
    SetError(&completer, MAGMA_STATUS_INVALID_ARGS);
    return;
  }

  if (!delegate_->ReleaseObject(request->object_id, *object_type))
    SetError(&completer, MAGMA_STATUS_INVALID_ARGS);
}

void ZirconPlatformConnection::CreateContext(CreateContextRequestView request,
                                             CreateContextCompleter::Sync& completer) {
  DLOG("ZirconPlatformConnection: CreateContext");
  FlowControl();

  magma::Status status = delegate_->CreateContext(request->context_id);
  if (!status.ok())
    SetError(&completer, status.get());
}

void ZirconPlatformConnection::DestroyContext(DestroyContextRequestView request,
                                              DestroyContextCompleter::Sync& completer) {
  DLOG("ZirconPlatformConnection: DestroyContext");
  FlowControl();

  magma::Status status = delegate_->DestroyContext(request->context_id);
  if (!status.ok())
    SetError(&completer, status.get());
}

void ZirconPlatformConnection::ExecuteCommand(ExecuteCommandRequestView request,
                                              ExecuteCommandCompleter::Sync& completer) {
  FlowControl();

  // TODO(fxbug.dev/92606) - support > 1 command buffer
  if (request->command_buffers.count() > 1) {
    SetError(&completer, MAGMA_STATUS_UNIMPLEMENTED);
    return;
  }

  auto command_buffer = std::make_unique<magma_command_buffer>();

  *command_buffer = {
      .resource_count = static_cast<uint32_t>(request->resources.count()),
      .batch_buffer_resource_index = request->command_buffers[0].resource_index,
      .batch_start_offset = request->command_buffers[0].start_offset,
      .wait_semaphore_count = static_cast<uint32_t>(request->wait_semaphores.count()),
      .signal_semaphore_count = static_cast<uint32_t>(request->signal_semaphores.count()),
      .flags = static_cast<uint64_t>(request->flags),
  };

  std::vector<magma_exec_resource> resources;
  resources.reserve(request->resources.count());

  for (auto& buffer_range : request->resources) {
    resources.push_back({
        buffer_range.buffer_id,
        buffer_range.offset,
        buffer_range.size,
    });
  }

  // Merge semaphores into one vector
  std::vector<uint64_t> semaphores;
  semaphores.reserve(request->wait_semaphores.count() + request->signal_semaphores.count());

  for (uint64_t semaphore_id : request->wait_semaphores) {
    semaphores.push_back(semaphore_id);
  }
  for (uint64_t semaphore_id : request->signal_semaphores) {
    semaphores.push_back(semaphore_id);
  }

  magma::Status status = delegate_->ExecuteCommandBufferWithResources(
      request->context_id, std::move(command_buffer), std::move(resources), std::move(semaphores));

  if (!status)
    SetError(&completer, status.get());
}

void ZirconPlatformConnection::ExecuteImmediateCommands(
    ExecuteImmediateCommandsRequestView request,
    ExecuteImmediateCommandsCompleter::Sync& completer) {
  DLOG("ZirconPlatformConnection: ExecuteImmediateCommands");
  FlowControl();

  magma::Status status = delegate_->ExecuteImmediateCommands(
      request->context_id, request->command_data.count(), request->command_data.data(),
      request->semaphores.count(), request->semaphores.data());
  if (!status)
    SetError(&completer, status.get());
}

void ZirconPlatformConnection::Flush(FlushCompleter::Sync& completer) {
  DLOG("ZirconPlatformConnection: Flush");
  completer.Reply();
}

void ZirconPlatformConnection::MapBuffer(MapBufferRequestView request,
                                         MapBufferCompleter::Sync& completer) {
  DLOG("ZirconPlatformConnection: MapBufferFIDL");
  FlowControl();

  if (!request->has_range() || !request->has_hw_va()) {
    SetError(&completer, MAGMA_STATUS_INVALID_ARGS);
    return;
  }

  auto flags = request->has_flags() ? static_cast<uint64_t>(request->flags()) : 0;

  magma::Status status =
      delegate_->MapBuffer(request->range().buffer_id, request->hw_va(), request->range().offset,
                           request->range().size, flags);
  if (!status.ok())
    SetError(&completer, status.get());
}

void ZirconPlatformConnection::UnmapBuffer(UnmapBufferRequestView request,
                                           UnmapBufferCompleter::Sync& completer) {
  DLOG("ZirconPlatformConnection: UnmapBufferFIDL");
  FlowControl();

  if (!request->has_buffer_id() || !request->has_hw_va()) {
    SetError(&completer, MAGMA_STATUS_INVALID_ARGS);
    return;
  }

  magma::Status status = delegate_->UnmapBuffer(request->buffer_id(), request->hw_va());
  if (!status.ok())
    SetError(&completer, status.get());
}

void ZirconPlatformConnection::BufferRangeOp2(BufferRangeOp2RequestView request,
                                              BufferRangeOp2Completer::Sync& completer) {
  DLOG("ZirconPlatformConnection:::BufferRangeOp2");
  FlowControl();

  std::optional<int> buffer_op = GetBufferOp(request->op);
  if (!buffer_op) {
    SetError(&completer, MAGMA_STATUS_INVALID_ARGS);
    return;
  }

  magma::Status status = delegate_->BufferRangeOp(request->range.buffer_id, *buffer_op,
                                                  request->range.offset, request->range.size);

  if (!status) {
    SetError(&completer, status.get());
  }
}

void ZirconPlatformConnection::EnablePerformanceCounterAccess(
    EnablePerformanceCounterAccessRequestView request,
    EnablePerformanceCounterAccessCompleter::Sync& completer) {
  DLOG("ZirconPlatformConnection:::EnablePerformanceCounterAccess");
  FlowControl();

  magma::Status status = delegate_->EnablePerformanceCounterAccess(
      magma::PlatformHandle::Create(request->access_token.release()));
  if (!status) {
    SetError(&completer, status.get());
  }
}

void ZirconPlatformConnection::IsPerformanceCounterAccessAllowed(
    IsPerformanceCounterAccessAllowedCompleter::Sync& completer) {
  DLOG("ZirconPlatformConnection:::IsPerformanceCounterAccessAllowed");
  completer.Reply(delegate_->IsPerformanceCounterAccessAllowed());
}

void ZirconPlatformConnection::EnablePerformanceCounters(
    EnablePerformanceCountersRequestView request,
    EnablePerformanceCountersCompleter::Sync& completer) {
  FlowControl();
  magma::Status status =
      delegate_->EnablePerformanceCounters(request->counters.data(), request->counters.count());
  if (!status) {
    SetError(&completer, status.get());
  }
}

void ZirconPlatformConnection::CreatePerformanceCounterBufferPool(
    CreatePerformanceCounterBufferPoolRequestView request,
    CreatePerformanceCounterBufferPoolCompleter::Sync& completer) {
  FlowControl();
  auto pool = std::make_unique<ZirconPlatformPerfCountPool>(request->pool_id,
                                                            request->event_channel.TakeChannel());
  magma::Status status = delegate_->CreatePerformanceCounterBufferPool(std::move(pool));
  if (!status) {
    SetError(&completer, status.get());
  }
}

void ZirconPlatformConnection::ReleasePerformanceCounterBufferPool(
    ReleasePerformanceCounterBufferPoolRequestView request,
    ReleasePerformanceCounterBufferPoolCompleter::Sync& completer) {
  FlowControl();
  magma::Status status = delegate_->ReleasePerformanceCounterBufferPool(request->pool_id);
  if (!status) {
    SetError(&completer, status.get());
  }
}

void ZirconPlatformConnection::AddPerformanceCounterBufferOffsetsToPool(
    AddPerformanceCounterBufferOffsetsToPoolRequestView request,
    AddPerformanceCounterBufferOffsetsToPoolCompleter::Sync& completer) {
  FlowControl();
  for (auto& offset : request->offsets) {
    magma::Status status = delegate_->AddPerformanceCounterBufferOffsetToPool(
        request->pool_id, offset.buffer_id, offset.offset, offset.size);
    if (!status) {
      SetError(&completer, status.get());
    }
  }
}

void ZirconPlatformConnection::RemovePerformanceCounterBufferFromPool(
    RemovePerformanceCounterBufferFromPoolRequestView request,
    RemovePerformanceCounterBufferFromPoolCompleter::Sync& completer) {
  FlowControl();
  magma::Status status =
      delegate_->RemovePerformanceCounterBufferFromPool(request->pool_id, request->buffer_id);
  if (!status) {
    SetError(&completer, status.get());
  }
}

void ZirconPlatformConnection::DumpPerformanceCounters(
    DumpPerformanceCountersRequestView request, DumpPerformanceCountersCompleter::Sync& completer) {
  FlowControl();
  magma::Status status = delegate_->DumpPerformanceCounters(request->pool_id, request->trigger_id);
  if (!status) {
    SetError(&completer, status.get());
  }
}

void ZirconPlatformConnection::ClearPerformanceCounters(
    ClearPerformanceCountersRequestView request,
    ClearPerformanceCountersCompleter::Sync& completer) {
  FlowControl();
  magma::Status status =
      delegate_->ClearPerformanceCounters(request->counters.data(), request->counters.count());
  if (!status) {
    SetError(&completer, status.get());
  }
}

std::shared_ptr<PlatformConnection> PlatformConnection::Create(
    std::unique_ptr<PlatformConnection::Delegate> delegate, msd_client_id_t client_id,
    std::unique_ptr<magma::PlatformHandle> thread_profile,
    std::unique_ptr<magma::PlatformHandle> server_endpoint,
    std::unique_ptr<magma::PlatformHandle> server_notification_endpoint) {
  if (!delegate)
    return DRETP(nullptr, "attempting to create PlatformConnection with null delegate");

  auto shutdown_event = magma::PlatformEvent::Create();
  if (!shutdown_event)
    return DRETP(nullptr, "Failed to create shutdown event");

  auto connection = std::make_shared<ZirconPlatformConnection>(
      std::move(delegate), client_id, zx::channel(server_notification_endpoint->release()),
      std::shared_ptr<magma::PlatformEvent>(std::move(shutdown_event)), std::move(thread_profile));

  if (!connection->Bind(zx::channel(server_endpoint->release())))
    return DRETP(nullptr, "Bind failed");

  if (!connection->BeginShutdownWait())
    return DRETP(nullptr, "Failed to begin shutdown wait");

  return connection;
}

}  // namespace magma
