// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_connection_client.h"

#include <vector>

#include "magma_common_defs.h"
#include "magma_util/dlog.h"
#include "platform_connection_client.h"
#include "zircon_platform_handle.h"
#include "zircon_platform_status.h"

// clang-format off
using fuchsia_gpu_magma::wire::QueryId;
static_assert(static_cast<uint64_t>(QueryId::kVendorId) == MAGMA_QUERY_VENDOR_ID, "mismatch");
static_assert(static_cast<uint64_t>(QueryId::kDeviceId) == MAGMA_QUERY_DEVICE_ID, "mismatch");
static_assert(static_cast<uint64_t>(QueryId::kIsTotalTimeSupported) == MAGMA_QUERY_IS_TOTAL_TIME_SUPPORTED, "mismatch");
static_assert(static_cast<uint64_t>(QueryId::kMaximumInflightParams) == MAGMA_QUERY_MAXIMUM_INFLIGHT_PARAMS, "mismatch");
using fuchsia_gpu_magma::wire::MapFlags;
static_assert(static_cast<uint64_t>(MapFlags::kRead) == MAGMA_MAP_FLAG_READ, "mismatch");
static_assert(static_cast<uint64_t>(MapFlags::kWrite) == MAGMA_MAP_FLAG_WRITE, "mismatch");
static_assert(static_cast<uint64_t>(MapFlags::kExecute) == MAGMA_MAP_FLAG_EXECUTE, "mismatch");
static_assert(static_cast<uint64_t>(MapFlags::kGrowable) == MAGMA_MAP_FLAG_GROWABLE, "mismatch");
static_assert(static_cast<uint64_t>(MapFlags::kVendorFlag0) == MAGMA_MAP_FLAG_VENDOR_0, "mismatch");

// clang-format on

namespace magma {

class ZirconPlatformPerfCountPoolClient : public PlatformPerfCountPoolClient {
 public:
  zx_status_t Initialize() {
    static std::atomic_uint64_t ids;
    pool_id_ = ids++;
    zx::channel client_endpoint;
    zx_status_t status = zx::channel::create(0, &client_endpoint, &server_endpoint_);
    if (status == ZX_OK)
      perf_counter_events_ = fidl::WireSyncClient(
          fidl::ClientEnd<fuchsia_gpu_magma::PerformanceCounterEvents>(std::move(client_endpoint)));
    return status;
  }
  uint64_t pool_id() override { return pool_id_; }
  zx::channel TakeServerEndpoint() {
    DASSERT(server_endpoint_);
    return std::move(server_endpoint_);
  }

  magma_handle_t handle() override { return perf_counter_events_.client_end().channel().get(); }
  magma::Status ReadPerformanceCounterCompletion(uint32_t* trigger_id_out, uint64_t* buffer_id_out,
                                                 uint32_t* buffer_offset_out, uint64_t* time_out,
                                                 uint32_t* result_flags_out) override {
    zx_signals_t pending;
    zx_status_t status = perf_counter_events_.client_end().channel().wait_one(
        ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, zx::time(), &pending);
    if (status != ZX_OK) {
      magma_status_t magma_status = magma::FromZxStatus(status).get();
      return DRET(magma_status);
    }
    if (!(pending & ZX_CHANNEL_READABLE)) {
      // If none of the signals were active then wait_one should have returned ZX_ERR_TIMED_OUT.
      DASSERT(pending & ZX_CHANNEL_PEER_CLOSED);
      return DRET(MAGMA_STATUS_CONNECTION_LOST);
    }

    class EventHandler
        : public fidl::WireSyncEventHandler<fuchsia_gpu_magma::PerformanceCounterEvents> {
     public:
      EventHandler(uint32_t* trigger_id_out, uint64_t* buffer_id_out, uint32_t* buffer_offset_out,
                   uint64_t* time_out, uint32_t* result_flags_out)
          : trigger_id_out_(trigger_id_out),
            buffer_id_out_(buffer_id_out),
            buffer_offset_out_(buffer_offset_out),
            time_out_(time_out),
            result_flags_out_(result_flags_out) {}

      void OnPerformanceCounterReadCompleted(
          fidl::WireEvent<
              fuchsia_gpu_magma::PerformanceCounterEvents::OnPerformanceCounterReadCompleted>*
              event) override {
        if (!event->has_trigger_id() || !event->has_buffer_id() || !event->has_buffer_offset() ||
            !event->has_timestamp() || !event->has_flags()) {
          return;
        }
        *trigger_id_out_ = event->trigger_id();
        *buffer_id_out_ = event->buffer_id();
        *buffer_offset_out_ = event->buffer_offset();
        *time_out_ = event->timestamp();
        *result_flags_out_ = static_cast<uint32_t>(event->flags());
      }

     private:
      uint32_t* const trigger_id_out_;
      uint64_t* const buffer_id_out_;
      uint32_t* const buffer_offset_out_;
      uint64_t* const time_out_;
      uint32_t* const result_flags_out_;
    };

    EventHandler event_handler(trigger_id_out, buffer_id_out, buffer_offset_out, time_out,
                               result_flags_out);
    fidl::Status event_status = perf_counter_events_.HandleOneEvent(event_handler);
    if (!event_status.ok() && event_status.reason() == fidl::Reason::kUnexpectedMessage) {
      status = ZX_ERR_INTERNAL;
    } else {
      status = event_status.status();
    }
    magma_status_t magma_status = magma::FromZxStatus(status).get();
    return DRET(magma_status);
  }

 private:
  uint64_t pool_id_;

  fidl::WireSyncClient<fuchsia_gpu_magma::PerformanceCounterEvents> perf_counter_events_;
  zx::channel server_endpoint_;
};

void PrimaryWrapper::on_fidl_error(::fidl::UnbindInfo info) {
  unbind_info_ = info;
  loop_.Quit();
}

void PrimaryWrapper::OnNotifyMessagesConsumed(
    ::fidl::WireEvent<::fuchsia_gpu_magma::Primary::OnNotifyMessagesConsumed>* event) {
  inflight_count_ -= event->count;
}

void PrimaryWrapper::OnNotifyMemoryImported(
    ::fidl::WireEvent<::fuchsia_gpu_magma::Primary::OnNotifyMemoryImported>* event) {
  inflight_bytes_ -= event->bytes;
}

PrimaryWrapper::PrimaryWrapper(zx::channel channel, uint64_t max_inflight_messages,
                               uint64_t max_inflight_bytes)
    : loop_(&kAsyncLoopConfigNeverAttachToThread),
      client_(fidl::ClientEnd<fuchsia_gpu_magma::Primary>(std::move(channel)), loop_.dispatcher(),
              this),
      max_inflight_messages_(max_inflight_messages),
      max_inflight_bytes_(max_inflight_bytes) {
  if (max_inflight_messages == 0 || max_inflight_bytes == 0)
    return;

  zx_status_t status = client_->EnableFlowControl().status();
  if (status == ZX_OK) {
    flow_control_enabled_ = true;
  } else {
    MAGMA_LOG(ERROR, "EnableFlowControl failed: %d", status);
  }
}

static_assert(static_cast<uint32_t>(magma::PlatformObject::SEMAPHORE) ==
              static_cast<uint32_t>(fuchsia_gpu_magma::wire::ObjectType::kEvent));
static_assert(static_cast<uint32_t>(magma::PlatformObject::BUFFER) ==
              static_cast<uint32_t>(fuchsia_gpu_magma::wire::ObjectType::kBuffer));

magma_status_t PrimaryWrapper::ImportObject(zx::handle handle,
                                            magma::PlatformObject::Type object_type,
                                            uint64_t object_id) {
  uint64_t size = 0;

  if (object_type == magma::PlatformObject::BUFFER) {
    zx::unowned_vmo vmo(handle.get());
    zx_status_t status = vmo->get_size(&size);
    if (status != ZX_OK)
      return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "get_size failed: %d", status);
  }

  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl(size);

  auto wire_object_type = static_cast<fuchsia_gpu_magma::wire::ObjectType>(object_type);

  zx_status_t status =
      client_->ImportObject2(std::move(handle), wire_object_type, object_id).status();
  if (status == ZX_OK) {
    UpdateFlowControl(size);
  }
  return magma::FromZxStatus(status).get();
}

magma_status_t PrimaryWrapper::ReleaseObject(uint64_t object_id,
                                             magma::PlatformObject::Type object_type) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();

  auto wire_object_type = static_cast<fuchsia_gpu_magma::wire::ObjectType>(object_type);

  zx_status_t status = client_->ReleaseObject(object_id, wire_object_type).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return magma::FromZxStatus(status).get();
}

magma_status_t PrimaryWrapper::CreateContext(uint32_t context_id) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_->CreateContext(context_id).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return magma::FromZxStatus(status).get();
}

magma_status_t PrimaryWrapper::DestroyContext(uint32_t context_id) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_->DestroyContext(context_id).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return magma::FromZxStatus(status).get();
}

static_assert(static_cast<uint64_t>(fuchsia_gpu_magma::wire::CommandBufferFlags::kVendorFlag0) ==
              MAGMA_COMMAND_BUFFER_VENDOR_FLAGS_0);

magma_status_t PrimaryWrapper::ExecuteCommand(
    uint32_t context_id, ::fidl::VectorView<fuchsia_gpu_magma::wire::BufferRange> resources,
    ::fidl::VectorView<fuchsia_gpu_magma::wire::CommandBuffer> command_buffers,
    ::fidl::VectorView<uint64_t> wait_semaphores, ::fidl::VectorView<uint64_t> signal_semaphores,
    fuchsia_gpu_magma::wire::CommandBufferFlags flags) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status =
      client_
          ->ExecuteCommand(context_id, std::move(resources), std::move(command_buffers),
                           std::move(wait_semaphores), std::move(signal_semaphores), flags)
          .status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return magma::FromZxStatus(status).get();
}

magma_status_t PrimaryWrapper::ExecuteImmediateCommands(uint32_t context_id,
                                                        ::fidl::VectorView<uint8_t> command_data,
                                                        ::fidl::VectorView<uint64_t> semaphores) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status =
      client_->ExecuteImmediateCommands(context_id, std::move(command_data), std::move(semaphores))
          .status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return magma::FromZxStatus(status).get();
}

magma_status_t PrimaryWrapper::MapBuffer(uint64_t buffer_id, uint64_t hw_va, uint64_t offset,
                                         uint64_t length, fuchsia_gpu_magma::wire::MapFlags flags) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();

  fuchsia_gpu_magma::wire::BufferRange range = {
      .buffer_id = buffer_id, .offset = offset, .size = length};

  fidl::Arena allocator;
  auto builder = fuchsia_gpu_magma::wire::PrimaryMapBufferRequest::Builder(allocator);
  builder.hw_va(hw_va).range(range).flags(flags);

  zx_status_t status = client_->MapBuffer(builder.Build()).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return magma::FromZxStatus(status).get();
}

magma_status_t PrimaryWrapper::UnmapBuffer(uint64_t buffer_id, uint64_t hw_va) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();

  fidl::Arena allocator;
  auto builder = fuchsia_gpu_magma::wire::PrimaryUnmapBufferRequest::Builder(allocator);
  builder.hw_va(hw_va).buffer_id(buffer_id);

  zx_status_t status = client_->UnmapBuffer(builder.Build()).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return magma::FromZxStatus(status).get();
}

magma_status_t PrimaryWrapper::BufferRangeOp(uint64_t buffer_id,
                                             fuchsia_gpu_magma::wire::BufferOp op, uint64_t offset,
                                             uint64_t length) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();

  fuchsia_gpu_magma::wire::BufferRange range = {
      .buffer_id = buffer_id, .offset = offset, .size = length};

  zx_status_t status = client_->BufferRangeOp2(op, range).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return magma::FromZxStatus(status).get();
}

magma_status_t PrimaryWrapper::EnablePerformanceCounterAccess(zx::event event) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_->EnablePerformanceCounterAccess(std::move(event)).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return magma::FromZxStatus(status).get();
}

magma_status_t PrimaryWrapper::EnablePerformanceCounters(fidl::VectorView<uint64_t> counters) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_->EnablePerformanceCounters(std::move(counters)).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return magma::FromZxStatus(status).get();
}

magma_status_t PrimaryWrapper::CreatePerformanceCounterBufferPool(uint64_t pool_id,
                                                                  zx::channel event_channel) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status =
      client_
          ->CreatePerformanceCounterBufferPool(
              pool_id, fidl::ServerEnd<fuchsia_gpu_magma::PerformanceCounterEvents>(
                           std::move(event_channel)))
          .status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return magma::FromZxStatus(status).get();
}

magma_status_t PrimaryWrapper::ReleasePerformanceCounterBufferPool(uint64_t pool_id) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_->ReleasePerformanceCounterBufferPool(pool_id).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return magma::FromZxStatus(status).get();
}

magma_status_t PrimaryWrapper::AddPerformanceCounterBufferOffsetsToPool(
    uint64_t pool_id, fidl::VectorView<fuchsia_gpu_magma::wire::BufferRange> offsets) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status =
      client_->AddPerformanceCounterBufferOffsetsToPool(pool_id, std::move(offsets)).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return magma::FromZxStatus(status).get();
}

magma_status_t PrimaryWrapper::RemovePerformanceCounterBufferFromPool(uint64_t pool_id,
                                                                      uint64_t buffer_id) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_->RemovePerformanceCounterBufferFromPool(pool_id, buffer_id).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return magma::FromZxStatus(status).get();
}

magma_status_t PrimaryWrapper::DumpPerformanceCounters(uint64_t pool_id, uint32_t trigger_id) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_->DumpPerformanceCounters(pool_id, trigger_id).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return magma::FromZxStatus(status).get();
}

magma_status_t PrimaryWrapper::ClearPerformanceCounters(fidl::VectorView<uint64_t> counters) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_->ClearPerformanceCounters(std::move(counters)).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return magma::FromZxStatus(status).get();
}

std::tuple<bool, uint64_t, uint64_t> PrimaryWrapper::ShouldWait(uint64_t new_bytes) {
  uint64_t count = inflight_count_ + 1;
  uint64_t bytes = inflight_bytes_ + new_bytes;

  if (count > max_inflight_messages_) {
    return {true, count, bytes};
  }

  if (new_bytes && inflight_bytes_ < max_inflight_bytes_ / 2) {
    // Don't block because we won't get a return message.
    // Its ok to exceed the max inflight bytes in order to get very large messages through.
    return {false, count, bytes};
  }

  return {new_bytes && bytes > max_inflight_bytes_, count, bytes};
}

void PrimaryWrapper::FlowControl(uint64_t new_bytes) {
  if (!flow_control_enabled_)
    return;

  auto [wait, count, bytes] = ShouldWait(new_bytes);

  const auto wait_time_start = std::chrono::steady_clock::now();

  while (true) {
    if (wait) {
      DLOG("Flow control: waiting message count %lu (max %lu) bytes %lu (max %lu) new_bytes %lu",
           count, max_inflight_messages_, bytes, max_inflight_bytes_, new_bytes);
    }

    // Flow control messages are handled in the PrimaryWrapper::AsyncHandler.
    zx_status_t status = loop_.Run(
        wait ? zx::deadline_after(zx::sec(5)) : zx::deadline_after(zx::duration(0)), true /*once*/);
    if (status == ZX_ERR_BAD_STATE || status == ZX_ERR_CANCELED) {
      DLOG("Loop was shutdown, client is unbound (channel closed)");
      return;
    }
    if (wait && status == ZX_ERR_TIMED_OUT) {
      MAGMA_LOG(WARNING, "Flow control: timed out messages %lu bytes %lu", count, bytes);
      continue;
    }
    if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
      MAGMA_LOG(ERROR, "Flow control: error waiting for message: %d", status);
      return;
    }
    if (wait) {
      DLOG("Flow control: waited %lld us message count %lu (max %lu) imported bytes %lu (max %lu)",
           std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                 wait_time_start)
               .count(),
           count, max_inflight_messages_, bytes, max_inflight_bytes_);
    }

    std::tie(wait, count, bytes) = ShouldWait(new_bytes);
    if (!wait)
      break;
  }
}

void PrimaryWrapper::UpdateFlowControl(uint64_t new_bytes) {
  if (!flow_control_enabled_)
    return;
  inflight_count_ += 1;
  inflight_bytes_ += new_bytes;
}

magma_status_t PrimaryWrapper::Flush() {
  auto result = client_.sync()->Flush();
  return magma::FromZxStatus(result.status()).get();
}

magma_status_t PrimaryWrapper::GetError() {
  std::lock_guard<std::mutex> lock(get_error_lock_);
  if (error_ != MAGMA_STATUS_OK)
    return error_;

  auto result = client_.sync()->Flush();

  if (!result.ok()) {
    // Only run the loop if the channel has been closed - we don't want to process any
    // incoming flow control events here.
    DASSERT(result.status() == ZX_ERR_PEER_CLOSED || result.status() == ZX_ERR_CANCELED);

    // Ensure no other threads are waiting for async events via flow control
    std::lock_guard<std::mutex> loop_lock(flow_control_mutex_);

    // Run the loop to process the unbind
    loop_.RunUntilIdle();

    auto unbind_info = unbind_info_;
    DASSERT(unbind_info);
    if (unbind_info) {
      DMESSAGE("Primary protocol unbind_info: %s", unbind_info->FormatDescription().c_str());
      error_ = MAGMA_STATUS_INTERNAL_ERROR;
      if (unbind_info->is_peer_closed()) {
        error_ = magma::FromZxStatus(unbind_info->status()).get();
      }
    }
  }

  return error_;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

class ZirconPlatformConnectionClient : public PlatformConnectionClient {
 public:
  ZirconPlatformConnectionClient(zx::channel channel, zx::channel notification_channel,
                                 uint64_t max_inflight_messages, uint64_t max_inflight_bytes)
      : client_(std::move(channel), max_inflight_messages, max_inflight_bytes),
        notification_channel_(std::move(notification_channel)) {}

  magma_status_t ImportObject(uint32_t handle, PlatformObject::Type object_type,
                              uint64_t object_id) override {
    DLOG("ZirconPlatformConnectionClient: ImportObject");
    magma_status_t result = client_.ImportObject(zx::handle(handle), object_type, object_id);

    if (result != MAGMA_STATUS_OK)
      return DRET_MSG(result, "failed to write to channel");

    return MAGMA_STATUS_OK;
  }

  magma_status_t ReleaseObject(uint64_t object_id, PlatformObject::Type object_type) override {
    DLOG("ZirconPlatformConnectionClient: ReleaseObject");
    magma_status_t result = client_.ReleaseObject(object_id, object_type);

    if (result != MAGMA_STATUS_OK)
      return DRET_MSG(result, "failed to write to channel");

    return MAGMA_STATUS_OK;
  }

  // Creates a context and returns the context id
  magma_status_t CreateContext(uint32_t* context_id_out) override {
    DLOG("ZirconPlatformConnectionClient: CreateContext");
    auto context_id = next_context_id_++;
    *context_id_out = context_id;
    magma_status_t result = client_.CreateContext(context_id);
    return result;
  }

  // Destroys a context for the given id
  magma_status_t DestroyContext(uint32_t context_id) override {
    DLOG("ZirconPlatformConnectionClient: DestroyContext");
    magma_status_t result = client_.DestroyContext(context_id);
    return result;
  }

  magma_status_t ExecuteCommand(uint32_t context_id,
                                magma_command_descriptor* descriptor) override {
    std::vector<fuchsia_gpu_magma::wire::BufferRange> fidl_resources;
    fidl_resources.reserve(descriptor->resource_count);

    for (uint32_t i = 0; i < descriptor->resource_count; i++) {
      fidl_resources.push_back({.buffer_id = descriptor->resources[i].buffer_id,
                                .offset = descriptor->resources[i].offset,
                                .size = descriptor->resources[i].length});
    }

    std::vector<fuchsia_gpu_magma::wire::CommandBuffer> command_buffers;
    command_buffers.reserve(descriptor->command_buffer_count);
    for (uint32_t i = 0; i < descriptor->command_buffer_count; i++) {
      command_buffers.push_back({.resource_index = descriptor->command_buffers[i].resource_index,
                                 .start_offset = descriptor->command_buffers[i].start_offset});
    }

    uint64_t* wait_semaphores = descriptor->semaphore_ids;
    uint64_t* signal_semaphores = descriptor->semaphore_ids + descriptor->wait_semaphore_count;

    magma_status_t result = client_.ExecuteCommand(
        context_id,
        fidl::VectorView<fuchsia_gpu_magma::wire::BufferRange>::FromExternal(fidl_resources),
        fidl::VectorView<fuchsia_gpu_magma::wire::CommandBuffer>::FromExternal(command_buffers),
        fidl::VectorView<uint64_t>::FromExternal(wait_semaphores, descriptor->wait_semaphore_count),
        fidl::VectorView<uint64_t>::FromExternal(signal_semaphores,
                                                 descriptor->signal_semaphore_count),
        static_cast<fuchsia_gpu_magma::wire::CommandBufferFlags>(descriptor->flags));

    return result;
  }

  // Returns the number of commands that will fit within |max_bytes|.
  static int FitCommands(const size_t max_bytes, const uint64_t num_buffers,
                         const magma_inline_command_buffer* buffers, const uint64_t starting_index,
                         uint64_t* command_bytes, uint32_t* num_semaphores) {
    int buffer_count = 0;
    uint64_t bytes_used = 0;
    *command_bytes = 0;
    *num_semaphores = 0;
    while (starting_index + buffer_count < num_buffers && bytes_used < max_bytes) {
      *command_bytes += buffers[starting_index + buffer_count].size;
      *num_semaphores += buffers[starting_index + buffer_count].semaphore_count;
      bytes_used = *command_bytes + *num_semaphores * sizeof(uint64_t);
      buffer_count++;
    }
    if (bytes_used > max_bytes)
      return (buffer_count - 1);
    return buffer_count;
  }

  magma_status_t ExecuteImmediateCommands(uint32_t context_id, uint64_t num_buffers,
                                          magma_inline_command_buffer* buffers,
                                          uint64_t* messages_sent_out) override {
    DLOG("ZirconPlatformConnectionClient: ExecuteImmediateCommands");
    uint64_t buffers_sent = 0;
    uint64_t messages_sent = 0;

    while (buffers_sent < num_buffers) {
      // Tally up the number of commands to send in this batch.
      uint64_t command_bytes = 0;
      uint32_t num_semaphores = 0;
      int buffers_to_send =
          FitCommands(fuchsia_gpu_magma::wire::kMaxImmediateCommandsDataSize, num_buffers, buffers,
                      buffers_sent, &command_bytes, &num_semaphores);

      // TODO(fxbug.dev/13144): Figure out how to move command and semaphore bytes across the FIDL
      //               interface without copying.
      std::vector<uint8_t> command_vec;
      command_vec.reserve(command_bytes);
      std::vector<uint64_t> semaphore_vec;
      semaphore_vec.reserve(num_semaphores);

      for (int i = 0; i < buffers_to_send; ++i) {
        const auto& buffer = buffers[buffers_sent + i];
        const auto buffer_data = static_cast<uint8_t*>(buffer.data);
        std::copy(buffer_data, buffer_data + buffer.size, std::back_inserter(command_vec));
        std::copy(buffer.semaphore_ids, buffer.semaphore_ids + buffer.semaphore_count,
                  std::back_inserter(semaphore_vec));
      }
      magma_status_t result = client_.ExecuteImmediateCommands(
          context_id, fidl::VectorView<uint8_t>::FromExternal(command_vec),
          fidl::VectorView<uint64_t>::FromExternal(semaphore_vec));
      if (result != MAGMA_STATUS_OK) {
        return result;
      }
      buffers_sent += buffers_to_send;
      messages_sent += 1;
    }

    *messages_sent_out = messages_sent;
    return MAGMA_STATUS_OK;
  }

  magma_status_t GetError() override {
    DLOG("ZirconPlatformConnectionClient: GetError");
    return client_.GetError();
  }

  magma_status_t Flush() override {
    DLOG("ZirconPlatformConnectionClient: Flush");
    return client_.Flush();
  }

  magma_status_t MapBuffer(uint64_t buffer_id, uint64_t hw_va, uint64_t offset, uint64_t length,
                           uint64_t flags) override {
    DLOG("ZirconPlatformConnectionClient: MapBuffer");
    magma_status_t result = client_.MapBuffer(
        buffer_id, hw_va, offset, length, static_cast<fuchsia_gpu_magma::wire::MapFlags>(flags));

    if (result != MAGMA_STATUS_OK)
      return DRET_MSG(result, "failed to write to channel");

    return MAGMA_STATUS_OK;
  }

  magma_status_t UnmapBuffer(uint64_t buffer_id, uint64_t hw_va) override {
    DLOG("ZirconPlatformConnectionClient: UnmapBuffer");
    magma_status_t result = client_.UnmapBuffer(buffer_id, hw_va);

    if (result != MAGMA_STATUS_OK)
      return DRET_MSG(result, "failed to write to channel");

    return MAGMA_STATUS_OK;
  }

  magma_status_t BufferRangeOp(uint64_t buffer_id, uint32_t options, uint64_t offset,
                               uint64_t length) override {
    DLOG("ZirconPlatformConnectionClient::BufferOpRange");
    fuchsia_gpu_magma::wire::BufferOp op;
    switch (options) {
      case MAGMA_BUFFER_RANGE_OP_DEPOPULATE_TABLES:
        op = fuchsia_gpu_magma::wire::BufferOp::kDepopulateTables;
        break;
      case MAGMA_BUFFER_RANGE_OP_POPULATE_TABLES:
        op = fuchsia_gpu_magma::wire::BufferOp::kPopulateTables;
        break;
      default:
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Invalid buffer op %d", options);
    }

    magma_status_t result = client_.BufferRangeOp(buffer_id, op, offset, length);

    if (result != MAGMA_STATUS_OK)
      return DRET_MSG(result, "failed to write to channel");

    return MAGMA_STATUS_OK;
  }

  magma_status_t EnablePerformanceCounterAccess(
      std::unique_ptr<magma::PlatformHandle> handle) override {
    if (!handle)
      return DRET(MAGMA_STATUS_INVALID_ARGS);

    zx::event event(static_cast<ZirconPlatformHandle*>(handle.get())->release());

    magma_status_t result = client_.EnablePerformanceCounterAccess(std::move(event));

    if (result != MAGMA_STATUS_OK)
      return DRET_MSG(result, "failed to write to channel");

    return MAGMA_STATUS_OK;
  }

  magma_status_t IsPerformanceCounterAccessAllowed(bool* enabled_out) override {
    auto rsp = client_.IsPerformanceCounterAccessAllowed();
    if (!rsp.ok())
      return DRET_MSG(magma::FromZxStatus(rsp.status()).get(), "failed to write to channel");

    *enabled_out = rsp->enabled;
    return MAGMA_STATUS_OK;
  }

  magma::Status EnablePerformanceCounters(uint64_t* counters, uint64_t counter_count) override {
    magma_status_t result = client_.EnablePerformanceCounters(
        fidl::VectorView<uint64_t>::FromExternal(counters, counter_count));

    if (result != MAGMA_STATUS_OK)
      return DRET(result);
    return MAGMA_STATUS_OK;
  }

  magma::Status CreatePerformanceCounterBufferPool(
      std::unique_ptr<PlatformPerfCountPoolClient>* pool_out) override {
    auto zircon_pool = std::make_unique<ZirconPlatformPerfCountPoolClient>();
    zx_status_t status = zircon_pool->Initialize();
    if (status != ZX_OK)
      return magma::FromZxStatus(status).get();

    magma_status_t result = client_.CreatePerformanceCounterBufferPool(
        zircon_pool->pool_id(), zircon_pool->TakeServerEndpoint());
    if (result != MAGMA_STATUS_OK)
      return DRET(result);
    *pool_out = std::move(zircon_pool);
    return MAGMA_STATUS_OK;
  }

  magma::Status ReleasePerformanceCounterBufferPool(uint64_t pool_id) override {
    magma_status_t result = client_.ReleasePerformanceCounterBufferPool(pool_id);
    if (result != MAGMA_STATUS_OK)
      return DRET(result);
    return MAGMA_STATUS_OK;
  }

  magma::Status AddPerformanceCounterBufferOffsetsToPool(uint64_t pool_id,
                                                         const magma_buffer_offset* offsets,
                                                         uint64_t offset_count) override {
    DASSERT(sizeof(*offsets) == sizeof(fuchsia_gpu_magma::wire::BufferRange));
    // The LLCPP FIDL bindings don't take const pointers, but they don't modify the data unless it
    // contains handles.
    auto fidl_offsets = const_cast<fuchsia_gpu_magma::wire::BufferRange*>(
        reinterpret_cast<const fuchsia_gpu_magma::wire::BufferRange*>(offsets));
    magma_status_t result = client_.AddPerformanceCounterBufferOffsetsToPool(
        pool_id, fidl::VectorView<fuchsia_gpu_magma::wire::BufferRange>::FromExternal(
                     fidl_offsets, offset_count));
    if (result != MAGMA_STATUS_OK)
      return DRET(result);
    return MAGMA_STATUS_OK;
  }

  magma::Status RemovePerformanceCounterBufferFromPool(uint64_t pool_id,
                                                       uint64_t buffer_id) override {
    magma_status_t result = client_.RemovePerformanceCounterBufferFromPool(pool_id, buffer_id);
    if (result != MAGMA_STATUS_OK)
      return DRET(result);
    return MAGMA_STATUS_OK;
  }

  magma::Status DumpPerformanceCounters(uint64_t pool_id, uint32_t trigger_id) override {
    magma_status_t result = client_.DumpPerformanceCounters(pool_id, trigger_id);
    if (result != MAGMA_STATUS_OK)
      return DRET(result);
    return MAGMA_STATUS_OK;
  }

  magma::Status ClearPerformanceCounters(uint64_t* counters, uint64_t counter_count) override {
    magma_status_t result = client_.ClearPerformanceCounters(
        fidl::VectorView<uint64_t>::FromExternal(counters, counter_count));
    if (result != MAGMA_STATUS_OK)
      return DRET(result);
    return MAGMA_STATUS_OK;
  }

  uint32_t GetNotificationChannelHandle() override { return notification_channel_.get(); }

  magma_status_t ReadNotificationChannel(void* buffer, size_t buffer_size, size_t* buffer_size_out,
                                         magma_bool_t* more_data_out) override {
    uint32_t buffer_actual_size;

    zx_status_t status = notification_channel_.read(
        0, buffer, nullptr, magma::to_uint32(buffer_size), 0, &buffer_actual_size, nullptr);
    switch (status) {
      case ZX_ERR_SHOULD_WAIT:
        *buffer_size_out = 0;
        *more_data_out = false;
        return MAGMA_STATUS_OK;
      case ZX_OK: {
        *buffer_size_out = buffer_actual_size;
        uint32_t null_buffer_size = 0;
        status = notification_channel_.read(0, buffer, nullptr, null_buffer_size, 0,
                                            &null_buffer_size, nullptr);
        *more_data_out = (status == ZX_ERR_BUFFER_TOO_SMALL);
        return MAGMA_STATUS_OK;
      }
      case ZX_ERR_PEER_CLOSED:
        return DRET_MSG(MAGMA_STATUS_CONNECTION_LOST, "notification channel, closed");
      case ZX_ERR_BUFFER_TOO_SMALL:
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "buffer_size %zd buffer_actual_size %u",
                        buffer_size, buffer_actual_size);
      default:
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "notification channel read failed, status %d",
                        status);
    }
  }

  std::pair<uint64_t, uint64_t> GetFlowControlCounts() override {
    return {client_.inflight_count(), client_.inflight_bytes()};
  }

 private:
  PrimaryWrapper client_;
  zx::channel notification_channel_;
  uint32_t next_context_id_ = 1;
};

std::unique_ptr<PlatformConnectionClient> PlatformConnectionClient::Create(
    uint32_t device_handle, uint32_t device_notification_handle, uint64_t max_inflight_messages,
    uint64_t max_inflight_bytes) {
  return std::unique_ptr<ZirconPlatformConnectionClient>(new ZirconPlatformConnectionClient(
      zx::channel(device_handle), zx::channel(device_notification_handle), max_inflight_messages,
      max_inflight_bytes));
}

// static
std::unique_ptr<magma::PlatformHandle> PlatformConnectionClient::RetrieveAccessToken(
    magma::PlatformHandle* channel) {
  if (!channel)
    return DRETP(nullptr, "No channel");
  auto rsp = fidl::WireCall(
                 fidl::UnownedClientEnd<fuchsia_gpu_magma::PerformanceCounterAccess>(
                     zx::unowned_channel(static_cast<const ZirconPlatformHandle*>(channel)->get())))
                 ->GetPerformanceCountToken();
  if (!rsp.ok()) {
    return DRETP(nullptr, "GetPerformanceCountToken failed");
  }
  if (!rsp->access_token) {
    return DRETP(nullptr, "GetPerformanceCountToken retrieved no event.");
  }
  return magma::PlatformHandle::Create(rsp->access_token.release());
}

}  // namespace magma
