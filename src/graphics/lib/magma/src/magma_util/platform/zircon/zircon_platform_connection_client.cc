// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_connection_client.h"

#include "magma_common_defs.h"
#include "magma_util/dlog.h"
#include "platform_connection_client.h"
#include "zircon_platform_handle.h"

// clang-format off
using llcpp::fuchsia::gpu::magma::QueryId;
static_assert(static_cast<uint32_t>(QueryId::VENDOR_ID) == MAGMA_QUERY_VENDOR_ID, "mismatch");
static_assert(static_cast<uint32_t>(QueryId::DEVICE_ID) == MAGMA_QUERY_DEVICE_ID, "mismatch");
static_assert(static_cast<uint32_t>(QueryId::IS_TEST_RESTART_SUPPORTED) == MAGMA_QUERY_IS_TEST_RESTART_SUPPORTED, "mismatch");
static_assert(static_cast<uint32_t>(QueryId::IS_TOTAL_TIME_SUPPORTED) == MAGMA_QUERY_IS_TOTAL_TIME_SUPPORTED, "mismatch");
static_assert(static_cast<uint32_t>(QueryId::MINIMUM_MAPPABLE_ADDRESS) == MAGMA_QUERY_MINIMUM_MAPPABLE_ADDRESS, "mismatch");
static_assert(static_cast<uint32_t>(QueryId::MAXIMUM_INFLIGHT_PARAMS) == MAGMA_QUERY_MAXIMUM_INFLIGHT_PARAMS, "mismatch");
// clang-format on

namespace {
// Convert zx channel status to magma status.
static magma_status_t MagmaChannelStatus(const zx_status_t status) {
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
}  // namespace

namespace magma {

class ZirconPlatformPerfCountPoolClient : public PlatformPerfCountPoolClient {
 public:
  zx_status_t Initialize() {
    static std::atomic_uint64_t ids;
    pool_id_ = ids++;
    zx::channel client_endpoint;
    zx_status_t status = zx::channel::create(0, &client_endpoint, &server_endpoint_);
    if (status == ZX_OK)
      perf_counter_events_ = llcpp::fuchsia::gpu::magma::PerformanceCounterEvents::SyncClient(
          std::move(client_endpoint));
    return status;
  }
  uint64_t pool_id() override { return pool_id_; }
  zx::channel TakeServerEndpoint() {
    DASSERT(server_endpoint_);
    return std::move(server_endpoint_);
  }

  magma_handle_t handle() override { return perf_counter_events_.channel().get(); }
  magma::Status ReadPerformanceCounterCompletion(uint32_t* trigger_id_out, uint64_t* buffer_id_out,
                                                 uint32_t* buffer_offset_out, uint64_t* time_out,
                                                 uint32_t* result_flags_out) override {
    zx_signals_t pending;
    zx_status_t status = perf_counter_events_.channel().wait_one(
        ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, zx::time(), &pending);
    if (status != ZX_OK) {
      magma_status_t magma_status = MagmaChannelStatus(status);
      return DRET(magma_status);
    }
    if (!(pending & ZX_CHANNEL_READABLE)) {
      // If none of the signals were active then wait_one should have returned ZX_ERR_TIMED_OUT.
      DASSERT(pending & ZX_CHANNEL_PEER_CLOSED);
      return DRET(MAGMA_STATUS_CONNECTION_LOST);
    }

    llcpp::fuchsia::gpu::magma::PerformanceCounterEvents::EventHandlers handlers;
    handlers.on_performance_counter_read_completed =
        [&](llcpp::fuchsia::gpu::magma::PerformanceCounterEvents::
                OnPerformanceCounterReadCompletedResponse* message) {
          *trigger_id_out = message->trigger_id;
          *buffer_id_out = message->buffer_id;
          *buffer_offset_out = message->buffer_offset;
          *time_out = message->timestamp;
          *result_flags_out = static_cast<uint32_t>(message->flags);
          return ZX_OK;
        };
    handlers.unknown = []() { return ZX_ERR_INTERNAL; };
    magma_status_t magma_status =
        MagmaChannelStatus(perf_counter_events_.HandleEvents(handlers).status());
    return DRET(magma_status);
  }

 private:
  uint64_t pool_id_;

  llcpp::fuchsia::gpu::magma::PerformanceCounterEvents::SyncClient perf_counter_events_;
  zx::channel server_endpoint_;
};

PrimaryWrapper::PrimaryWrapper(zx::channel channel, uint64_t max_inflight_messages,
                               uint64_t max_inflight_bytes)
    : client_(std::move(channel)),
      max_inflight_messages_(max_inflight_messages),
      max_inflight_bytes_(max_inflight_bytes) {
  if (max_inflight_messages == 0 || max_inflight_bytes == 0)
    return;

  zx_status_t status = client_.EnableFlowControl().status();
  if (status == ZX_OK) {
    flow_control_enabled_ = true;
  } else {
    MAGMA_LOG(ERROR, "EnableFlowControl failed: %d", status);
  }
}

magma_status_t PrimaryWrapper::ImportBuffer(zx::vmo vmo) {
  uint64_t size = 0;
  vmo.get_size(&size);

  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl(size);

  zx_status_t status = client_.ImportBuffer(std::move(vmo)).status();
  if (status == ZX_OK) {
    UpdateFlowControl(size);
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::ReleaseBuffer(uint64_t buffer_id) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_.ReleaseBuffer(buffer_id).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::ImportObject(zx::handle handle,
                                            magma::PlatformObject::Type object_type) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_.ImportObject(std::move(handle), object_type).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::ReleaseObject(uint64_t object_id,
                                             magma::PlatformObject::Type object_type) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_.ReleaseObject(object_id, object_type).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::CreateContext(uint32_t context_id) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_.CreateContext(context_id).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::DestroyContext(uint32_t context_id) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_.DestroyContext(context_id).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::ExecuteCommandBufferWithResources(
    uint32_t context_id, ::llcpp::fuchsia::gpu::magma::CommandBuffer command_buffer,
    ::fidl::VectorView<::llcpp::fuchsia::gpu::magma::Resource> resources,
    ::fidl::VectorView<uint64_t> wait_semaphores, ::fidl::VectorView<uint64_t> signal_semaphores) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_
                           .ExecuteCommandBufferWithResources(
                               context_id, std::move(command_buffer), std::move(resources),
                               std::move(wait_semaphores), std::move(signal_semaphores))
                           .status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::ExecuteImmediateCommands(uint32_t context_id,
                                                        ::fidl::VectorView<uint8_t> command_data,
                                                        ::fidl::VectorView<uint64_t> semaphores) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status =
      client_.ExecuteImmediateCommands(context_id, std::move(command_data), std::move(semaphores))
          .status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va,
                                            uint64_t page_offset, uint64_t page_count,
                                            uint64_t flags) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status =
      client_.MapBufferGpu(buffer_id, gpu_va, page_offset, page_count, flags).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_.UnmapBufferGpu(buffer_id, gpu_va).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::CommitBuffer(uint64_t buffer_id, uint64_t page_offset,
                                            uint64_t page_count) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_.CommitBuffer(buffer_id, page_offset, page_count).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::AccessPerformanceCounters(zx::event event) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_.AccessPerformanceCounters(std::move(event)).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::EnablePerformanceCounters(fidl::VectorView<uint64_t> counters) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_.EnablePerformanceCounters(std::move(counters)).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::CreatePerformanceCounterBufferPool(uint64_t pool_id,
                                                                  zx::channel event_channel) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status =
      client_.CreatePerformanceCounterBufferPool(pool_id, std::move(event_channel)).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::ReleasePerformanceCounterBufferPool(uint64_t pool_id) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_.ReleasePerformanceCounterBufferPool(pool_id).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::AddPerformanceCounterBufferOffsetsToPool(
    uint64_t pool_id, fidl::VectorView<llcpp::fuchsia::gpu::magma::BufferOffset> offsets) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status =
      client_.AddPerformanceCounterBufferOffsetsToPool(pool_id, std::move(offsets)).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::RemovePerformanceCounterBufferFromPool(uint64_t pool_id,
                                                                      uint64_t buffer_id) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_.RemovePerformanceCounterBufferFromPool(pool_id, buffer_id).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::DumpPerformanceCounters(uint64_t pool_id, uint32_t trigger_id) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_.DumpPerformanceCounters(pool_id, trigger_id).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
}

magma_status_t PrimaryWrapper::ClearPerformanceCounters(fidl::VectorView<uint64_t> counters) {
  std::lock_guard<std::mutex> lock(flow_control_mutex_);
  FlowControl();
  zx_status_t status = client_.ClearPerformanceCounters(std::move(counters)).status();
  if (status == ZX_OK) {
    UpdateFlowControl();
  }
  return MagmaChannelStatus(status);
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

    zx_signals_t pending = {};
    zx_status_t status = client_.channel().wait_one(
        ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
        wait ? zx::deadline_after(zx::sec(5)) : zx::deadline_after(zx::duration(0)), &pending);

    if (wait && status == ZX_ERR_TIMED_OUT) {
      MAGMA_LOG(WARNING, "Flow control: timed out messages %lu bytes %lu", count, bytes);
      continue;
    }
    if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
      MAGMA_LOG(ERROR, "Flow control: error waiting for message: %d", status);
      return;
    }

    if ((pending & ZX_CHANNEL_READABLE) == 0)
      return;

    llcpp::fuchsia::gpu::magma::Primary::EventHandlers event_handlers = {
        .on_notify_messages_consumed =
            [this](llcpp::fuchsia::gpu::magma::Primary::OnNotifyMessagesConsumedResponse* message) {
              inflight_count_ -= message->count;
              return ZX_OK;
            },
        .on_notify_memory_imported =
            [this](llcpp::fuchsia::gpu::magma::Primary::OnNotifyMemoryImportedResponse* message) {
              inflight_bytes_ -= message->bytes;
              return ZX_OK;
            },
        .unknown =
            []() {
              MAGMA_LOG(ERROR, "Flow control: bad event handler ordinal");
              return ZX_ERR_INVALID_ARGS;
            }};

    status = client_.HandleEvents(event_handlers).status();
    if (status != ZX_OK) {
      DMESSAGE("Flow control: HandleEvents failed: %d", status);
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

//////////////////////////////////////////////////////////////////////////////////////////////////

class ZirconPlatformConnectionClient : public PlatformConnectionClient {
 public:
  ZirconPlatformConnectionClient(zx::channel channel, zx::channel notification_channel,
                                 uint64_t max_inflight_messages, uint64_t max_inflight_bytes)
      : client_(std::move(channel), max_inflight_messages, max_inflight_bytes),
        notification_channel_(std::move(notification_channel)) {}

  // Imports a buffer for use in the system driver
  magma_status_t ImportBuffer(PlatformBuffer* buffer) override {
    DLOG("ZirconPlatformConnectionClient: ImportBuffer");
    if (!buffer)
      return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "attempting to import null buffer");

    uint32_t duplicate_handle;
    if (!buffer->duplicate_handle(&duplicate_handle))
      return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "failed to get duplicate_handle");

    zx::vmo vmo(duplicate_handle);
    magma_status_t result = client_.ImportBuffer(std::move(vmo));
    if (result != MAGMA_STATUS_OK) {
      return DRET_MSG(result, "failed to write to channel");
    }

    return MAGMA_STATUS_OK;
  }

  // Destroys the buffer with |buffer_id| within this connection
  // returns false if the buffer with |buffer_id| has not been imported
  magma_status_t ReleaseBuffer(uint64_t buffer_id) override {
    DLOG("ZirconPlatformConnectionClient: ReleaseBuffer");
    magma_status_t result = client_.ReleaseBuffer(buffer_id);

    if (result != MAGMA_STATUS_OK)
      return DRET_MSG(result, "failed to write to channel");

    return MAGMA_STATUS_OK;
  }

  magma_status_t ImportObject(uint32_t handle, PlatformObject::Type object_type) override {
    DLOG("ZirconPlatformConnectionClient: ImportObject");
    magma_status_t result = client_.ImportObject(zx::handle(handle), object_type);

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
  void CreateContext(uint32_t* context_id_out) override {
    DLOG("ZirconPlatformConnectionClient: CreateContext");
    auto context_id = next_context_id_++;
    *context_id_out = context_id;
    magma_status_t result = client_.CreateContext(context_id);
    if (result != MAGMA_STATUS_OK)
      SetError(result);
  }

  // Destroys a context for the given id
  void DestroyContext(uint32_t context_id) override {
    DLOG("ZirconPlatformConnectionClient: DestroyContext");
    magma_status_t result = client_.DestroyContext(context_id);
    if (result != MAGMA_STATUS_OK)
      SetError(result);
  }

  void ExecuteCommandBufferWithResources(uint32_t context_id,
                                         magma_system_command_buffer* command_buffer,
                                         magma_system_exec_resource* resources,
                                         uint64_t* semaphores) override {
    llcpp::fuchsia::gpu::magma::CommandBuffer fidl_command_buffer = {
        .batch_buffer_resource_index = command_buffer->batch_buffer_resource_index,
        .batch_start_offset = command_buffer->batch_start_offset};

    std::vector<llcpp::fuchsia::gpu::magma::Resource> fidl_resources;
    fidl_resources.reserve(command_buffer->resource_count);

    for (uint32_t i = 0; i < command_buffer->resource_count; i++) {
      fidl_resources.push_back({.buffer = resources[i].buffer_id,
                                .offset = resources[i].offset,
                                .length = resources[i].length});
    }

    uint64_t* wait_semaphores = semaphores;
    uint64_t* signal_semaphores = semaphores + command_buffer->wait_semaphore_count;

    magma_status_t result = client_.ExecuteCommandBufferWithResources(
        context_id, std::move(fidl_command_buffer), fidl::unowned_vec(fidl_resources),
        fidl::VectorView<uint64_t>(fidl::unowned_ptr(wait_semaphores),
                                   command_buffer->wait_semaphore_count),
        fidl::VectorView<uint64_t>(fidl::unowned_ptr(signal_semaphores),
                                   command_buffer->signal_semaphore_count));

    if (result != MAGMA_STATUS_OK)
      SetError(result);
  }

  // Returns the number of commands that will fit within |max_bytes|.
  static int FitCommands(const size_t max_bytes, const int num_buffers,
                         const magma_inline_command_buffer* buffers, const int starting_index,
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

  void ExecuteImmediateCommands(uint32_t context_id, uint64_t num_buffers,
                                magma_inline_command_buffer* buffers,
                                uint64_t* messages_sent_out) override {
    DLOG("ZirconPlatformConnectionClient: ExecuteImmediateCommands");
    uint64_t buffers_sent = 0;
    uint64_t messages_sent = 0;

    while (buffers_sent < num_buffers) {
      // Tally up the number of commands to send in this batch.
      uint64_t command_bytes = 0;
      uint32_t num_semaphores = 0;
      int buffers_to_send = FitCommands(llcpp::fuchsia::gpu::magma::kReceiveBufferSize, num_buffers,
                                        buffers, buffers_sent, &command_bytes, &num_semaphores);

      // TODO(MA-536): Figure out how to move command and semaphore bytes across the FIDL
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
          context_id, fidl::unowned_vec(command_vec), fidl::unowned_vec(semaphore_vec));
      if (result != MAGMA_STATUS_OK)
        SetError(result);
      buffers_sent += buffers_to_send;
      messages_sent += 1;
    }

    *messages_sent_out = messages_sent;
  }

  magma_status_t GetError() override {
    DLOG("ZirconPlatformConnectionClient: GetError");
    // We need a lock around the channel write and read, because otherwise it's possible two
    // threads will send the GetErrorOp, the first WaitError will get a response and read it,
    // and the second WaitError will wake up because of the first response and error out because
    // there's no message available to read yet.
    std::lock_guard<std::mutex> lock(get_error_lock_);
    magma_status_t error = error_;
    error_ = 0;
    if (error != MAGMA_STATUS_OK)
      return error;

    auto result = client_.GetError();
    magma_status_t status = MagmaChannelStatus(result.status());
    if (status != MAGMA_STATUS_OK)
      return status;
    return result.Unwrap()->magma_status;
  }

  magma_status_t MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset,
                              uint64_t page_count, uint64_t flags) override {
    DLOG("ZirconPlatformConnectionClient: MapBufferGpu");
    magma_status_t result = client_.MapBufferGpu(buffer_id, gpu_va, page_offset, page_count, flags);

    if (result != MAGMA_STATUS_OK)
      return DRET_MSG(result, "failed to write to channel");

    return MAGMA_STATUS_OK;
  }

  magma_status_t UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va) override {
    DLOG("ZirconPlatformConnectionClient: UnmapBufferGpu");
    magma_status_t result = client_.UnmapBufferGpu(buffer_id, gpu_va);

    if (result != MAGMA_STATUS_OK)
      return DRET_MSG(result, "failed to write to channel");

    return MAGMA_STATUS_OK;
  }

  magma_status_t CommitBuffer(uint64_t buffer_id, uint64_t page_offset,
                              uint64_t page_count) override {
    DLOG("ZirconPlatformConnectionClient: CommitBuffer");
    magma_status_t result = client_.CommitBuffer(buffer_id, page_offset, page_count);

    if (result != MAGMA_STATUS_OK)
      return DRET_MSG(result, "failed to write to channel");

    return MAGMA_STATUS_OK;
  }

  magma_status_t AccessPerformanceCounters(std::unique_ptr<magma::PlatformHandle> handle) override {
    if (!handle)
      return DRET(MAGMA_STATUS_INVALID_ARGS);

    zx::event event(static_cast<ZirconPlatformHandle*>(handle.get())->release());

    magma_status_t result = client_.AccessPerformanceCounters(std::move(event));

    if (result != MAGMA_STATUS_OK)
      return DRET_MSG(result, "failed to write to channel");

    return MAGMA_STATUS_OK;
  }

  magma_status_t IsPerformanceCounterAccessEnabled(bool* enabled_out) override {
    auto rsp = client_.IsPerformanceCounterAccessEnabled();
    if (!rsp.ok())
      return DRET_MSG(MagmaChannelStatus(rsp.status()), "failed to write to channel");

    *enabled_out = rsp->enabled;
    return MAGMA_STATUS_OK;
  }

  magma::Status EnablePerformanceCounters(uint64_t* counters, uint64_t counter_count) override {
    magma_status_t result = client_.EnablePerformanceCounters(
        fidl::VectorView<uint64_t>(fidl::unowned_ptr(counters), counter_count));

    if (result != MAGMA_STATUS_OK)
      return DRET(result);
    return MAGMA_STATUS_OK;
  }

  magma::Status CreatePerformanceCounterBufferPool(
      std::unique_ptr<PlatformPerfCountPoolClient>* pool_out) override {
    auto zircon_pool = std::make_unique<ZirconPlatformPerfCountPoolClient>();
    zx_status_t status = zircon_pool->Initialize();
    if (status != ZX_OK)
      return MagmaChannelStatus(status);
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
    DASSERT(sizeof(*offsets) == sizeof(llcpp::fuchsia::gpu::magma::BufferOffset));
    // The LLCPP FIDL bindings don't take const pointers, but they don't modify the data unless it
    // contains handles.
    auto fidl_offsets = const_cast<llcpp::fuchsia::gpu::magma::BufferOffset*>(
        reinterpret_cast<const llcpp::fuchsia::gpu::magma::BufferOffset*>(offsets));
    magma_status_t result = client_.AddPerformanceCounterBufferOffsetsToPool(
        pool_id, fidl::VectorView<llcpp::fuchsia::gpu::magma::BufferOffset>(
                     fidl::unowned_ptr(fidl_offsets), offset_count));
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
        fidl::VectorView<uint64_t>(fidl::unowned_ptr(counters), counter_count));
    if (result != MAGMA_STATUS_OK)
      return DRET(result);
    return MAGMA_STATUS_OK;
  }

  void SetError(magma_status_t error) {
    std::lock_guard<std::mutex> lock(get_error_lock_);
    if (!error_)
      error_ = DRET_MSG(error, "ZirconPlatformConnectionClient encountered dispatcher error");
  }

  uint32_t GetNotificationChannelHandle() override { return notification_channel_.get(); }

  magma_status_t ReadNotificationChannel(void* buffer, size_t buffer_size,
                                         size_t* buffer_size_out) override {
    uint32_t buffer_actual_size;
    zx_status_t status = notification_channel_.read(0, buffer, nullptr, buffer_size, 0,
                                                    &buffer_actual_size, nullptr);
    *buffer_size_out = buffer_actual_size;
    if (status == ZX_ERR_SHOULD_WAIT) {
      *buffer_size_out = 0;
      return MAGMA_STATUS_OK;
    } else if (status == ZX_OK) {
      return MAGMA_STATUS_OK;
    } else if (status == ZX_ERR_PEER_CLOSED) {
      return DRET_MSG(MAGMA_STATUS_CONNECTION_LOST, "notification channel, closed");
    } else {
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR,
                      "failed to wait on notification channel status %u", status);
    }
  }

  magma_status_t WaitNotificationChannel(int64_t timeout_ns) override {
    zx_signals_t pending;
    zx_status_t status =
        notification_channel_.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                       zx::deadline_after(zx::nsec(timeout_ns)), &pending);
    if (status != ZX_OK)
      return DRET(MagmaChannelStatus(status));
    if (pending & ZX_CHANNEL_READABLE)
      return MAGMA_STATUS_OK;
    if (pending & ZX_CHANNEL_PEER_CLOSED)
      return DRET(MAGMA_STATUS_CONNECTION_LOST);
    DASSERT(false);
    return MAGMA_STATUS_INTERNAL_ERROR;
  }

  std::pair<uint64_t, uint64_t> GetFlowControlCounts() override {
    return {client_.inflight_count(), client_.inflight_bytes()};
  }

 private:
  PrimaryWrapper client_;
  zx::channel notification_channel_;
  uint32_t next_context_id_ = 1;
  std::mutex get_error_lock_;
  MAGMA_GUARDED(get_error_lock_) magma_status_t error_{};
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
  auto rsp = llcpp::fuchsia::gpu::magma::PerformanceCounterAccess::Call::GetPerformanceCountToken(
      zx::unowned_channel(static_cast<const ZirconPlatformHandle*>(channel)->get()));
  if (!rsp.ok()) {
    return DRETP(nullptr, "GetPerformanceCountToken failed");
  }
  if (!rsp->access_token) {
    return DRETP(nullptr, "GetPerformanceCountToken retrieved no event.");
  }
  return magma::PlatformHandle::Create(rsp->access_token.release());
}

}  // namespace magma
