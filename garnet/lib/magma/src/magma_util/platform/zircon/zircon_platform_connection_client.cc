// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/gpu/magma/c/fidl.h>
#include <fuchsia/gpu/magma/cpp/fidl.h>

#include <mutex>

#include "platform_connection_client.h"

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

class ZirconPlatformConnectionClient : public PlatformConnectionClient {
 public:
  ZirconPlatformConnectionClient(zx::channel channel, zx::channel notification_channel)
      : notification_channel_(std::move(notification_channel)) {
    magma_fidl_.Bind(std::move(channel));
  }

  // Imports a buffer for use in the system driver
  magma_status_t ImportBuffer(PlatformBuffer* buffer) override {
    DLOG("ZirconPlatformConnectionClient: ImportBuffer");
    if (!buffer)
      return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "attempting to import null buffer");
    uint32_t duplicate_handle;
    if (!buffer->duplicate_handle(&duplicate_handle))
      return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "failed to get duplicate_handle");

    zx::vmo vmo(duplicate_handle);
    magma_status_t result = MagmaChannelStatus(magma_fidl_->ImportBuffer(std::move(vmo)));
    if (result != MAGMA_STATUS_OK) {
      return DRET_MSG(result, "failed to write to channel");
    }
    return MAGMA_STATUS_OK;
  }

  // Destroys the buffer with |buffer_id| within this connection
  // returns false if the buffer with |buffer_id| has not been imported
  magma_status_t ReleaseBuffer(uint64_t buffer_id) override {
    DLOG("ZirconPlatformConnectionClient: ReleaseBuffer");
    magma_status_t result = MagmaChannelStatus(magma_fidl_->ReleaseBuffer(buffer_id));
    if (result != MAGMA_STATUS_OK)
      return DRET_MSG(result, "failed to write to channel");
    return MAGMA_STATUS_OK;
  }

  magma_status_t ImportObject(uint32_t handle, PlatformObject::Type object_type) override {
    DLOG("ZirconPlatformConnectionClient: ImportObject");
    zx::handle duplicate_handle(handle);
    magma_status_t result =
        MagmaChannelStatus(magma_fidl_->ImportObject(std::move(duplicate_handle), object_type));
    if (result != MAGMA_STATUS_OK) {
      return DRET_MSG(result, "failed to write to channel");
    }
    return MAGMA_STATUS_OK;
  }

  magma_status_t ReleaseObject(uint64_t object_id, PlatformObject::Type object_type) override {
    DLOG("ZirconPlatformConnectionClient: ReleaseObject");
    magma_status_t result = MagmaChannelStatus(magma_fidl_->ReleaseObject(object_id, object_type));
    if (result != MAGMA_STATUS_OK)
      return DRET_MSG(result, "failed to write to channel");
    return MAGMA_STATUS_OK;
  }

  // Creates a context and returns the context id
  void CreateContext(uint32_t* context_id_out) override {
    DLOG("ZirconPlatformConnectionClient: CreateContext");
    auto context_id = next_context_id_++;
    *context_id_out = context_id;
    magma_status_t result = MagmaChannelStatus(magma_fidl_->CreateContext(context_id));
    if (result != MAGMA_STATUS_OK)
      SetError(result);
  }

  // Destroys a context for the given id
  void DestroyContext(uint32_t context_id) override {
    DLOG("ZirconPlatformConnectionClient: DestroyContext");
    magma_status_t result = MagmaChannelStatus(magma_fidl_->DestroyContext(context_id));
    if (result != MAGMA_STATUS_OK)
      SetError(result);
  }

  void ExecuteCommandBufferWithResources(uint32_t context_id,
                                         magma_system_command_buffer* command_buffer,
                                         magma_system_exec_resource* resources,
                                         uint64_t* semaphores) override {
    fuchsia::gpu::magma::CommandBuffer fidl_command_buffer = {
        .batch_buffer_resource_index = command_buffer->batch_buffer_resource_index,
        .batch_start_offset = command_buffer->batch_start_offset};

    std::vector<fuchsia::gpu::magma::Resource> fidl_resources;

    fidl_resources.reserve(command_buffer->num_resources);
    for (uint32_t i = 0; i < command_buffer->num_resources; i++) {
      fidl_resources.push_back({.buffer = resources[i].buffer_id,
                                .offset = resources[i].offset,
                                .length = resources[i].length});
    }

    std::vector<uint64_t> wait_semaphores;
    std::vector<uint64_t> signal_semaphores;

    wait_semaphores.reserve(command_buffer->wait_semaphore_count);
    signal_semaphores.reserve(command_buffer->signal_semaphore_count);

    uint32_t sem_index = 0;
    for (uint32_t i = 0; i < command_buffer->wait_semaphore_count; i++) {
      wait_semaphores.emplace_back(semaphores[sem_index++]);
    }
    for (uint32_t i = 0; i < command_buffer->signal_semaphore_count; i++) {
      signal_semaphores.emplace_back(semaphores[sem_index++]);
    }

    magma_status_t result = MagmaChannelStatus(magma_fidl_->ExecuteCommandBufferWithResources(
        context_id, std::move(fidl_command_buffer), std::move(fidl_resources),
        std::move(wait_semaphores), std::move(signal_semaphores)));
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
                                magma_inline_command_buffer* buffers) override {
    DLOG("ZirconPlatformConnectionClient: ExecuteImmediateCommands");
    uint64_t buffers_sent = 0;
    while (buffers_sent < num_buffers) {
      // Tally up the number of commands to send in this batch.
      uint64_t command_bytes = 0;
      uint32_t num_semaphores = 0;
      int buffers_to_send = FitCommands(fuchsia::gpu::magma::kReceiveBufferSize, num_buffers,
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
      magma_status_t result = MagmaChannelStatus(magma_fidl_->ExecuteImmediateCommands(
          context_id, std::move(command_vec), std::move(semaphore_vec)));
      if (result != MAGMA_STATUS_OK)
        SetError(result);
      buffers_sent += buffers_to_send;
    }
  }

  magma_status_t GetError() override {
    // We need a lock around the channel write and read, because otherwise it's possible two
    // threads will send the GetErrorOp, the first WaitError will get a response and read it,
    // and the second WaitError will wake up because of the first response and error out because
    // there's no message available to read yet.
    std::lock_guard<std::mutex> lock(get_error_lock_);
    magma_status_t error = error_;
    error_ = 0;
    if (error != MAGMA_STATUS_OK)
      return error;
    magma_status_t status = MagmaChannelStatus(magma_fidl_->GetError(&error));
    if (status != MAGMA_STATUS_OK)
      return status;
    return error;
  }

  magma_status_t MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset,
                              uint64_t page_count, uint64_t flags) override {
    DLOG("ZirconPlatformConnectionClient: MapBufferGpu");
    magma_status_t result = MagmaChannelStatus(
        magma_fidl_->MapBufferGpu(buffer_id, gpu_va, page_offset, page_count, flags));
    if (result != MAGMA_STATUS_OK)
      return DRET_MSG(result, "failed to write to channel");
    return MAGMA_STATUS_OK;
  }

  magma_status_t UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va) override {
    DLOG("ZirconPlatformConnectionClient: UnmapBufferGpu");
    magma_status_t result = MagmaChannelStatus(magma_fidl_->UnmapBufferGpu(buffer_id, gpu_va));
    if (result != MAGMA_STATUS_OK)
      return DRET_MSG(result, "failed to write to channel");
    return MAGMA_STATUS_OK;
  }

  magma_status_t CommitBuffer(uint64_t buffer_id, uint64_t page_offset,
                              uint64_t page_count) override {
    DLOG("ZirconPlatformConnectionClient: CommitBuffer");
    magma_status_t result =
        MagmaChannelStatus(magma_fidl_->CommitBuffer(buffer_id, page_offset, page_count));
    if (result != MAGMA_STATUS_OK)
      return DRET_MSG(result, "failed to write to channel");
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

 private:
  fuchsia::gpu::magma::PrimarySyncPtr magma_fidl_;
  zx::channel notification_channel_;
  uint32_t next_context_id_ = 1;
  std::mutex get_error_lock_;
  MAGMA_GUARDED(get_error_lock_) magma_status_t error_{};
};  // class ZirconPlatformConnectionClient

std::unique_ptr<PlatformConnectionClient> PlatformConnectionClient::Create(
    uint32_t device_handle, uint32_t device_notification_handle) {
  return std::unique_ptr<ZirconPlatformConnectionClient>(new ZirconPlatformConnectionClient(
      zx::channel(device_handle), zx::channel(device_notification_handle)));
}

}  // namespace magma
