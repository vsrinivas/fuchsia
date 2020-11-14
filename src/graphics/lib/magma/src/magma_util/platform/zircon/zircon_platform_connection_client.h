// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PLATFORM_CONNECTION_CLIENT_H
#define ZIRCON_PLATFORM_CONNECTION_CLIENT_H

#include <fuchsia/gpu/magma/llcpp/fidl.h>
#include <lib/zx/channel.h>

#include <mutex>

#include "platform_connection_client.h"

namespace magma {

// This wrapper gates access to the llcpp SyncClient, to ensure that all messages sent
// are subject to flow control.
class PrimaryWrapper {
 public:
  PrimaryWrapper(zx::channel channel, uint64_t max_inflight_messages, uint64_t max_inflight_bytes);

  magma_status_t ImportBuffer(zx::vmo vmo);
  magma_status_t ReleaseBuffer(uint64_t buffer_id);
  magma_status_t ImportObject(zx::handle handle, magma::PlatformObject::Type object_type);
  magma_status_t ReleaseObject(uint64_t object_id, magma::PlatformObject::Type object_type);
  magma_status_t CreateContext(uint32_t context_id);
  magma_status_t DestroyContext(uint32_t context_id);
  magma_status_t ExecuteCommandBufferWithResources(
      uint32_t context_id, ::llcpp::fuchsia::gpu::magma::CommandBuffer command_buffer,
      ::fidl::VectorView<::llcpp::fuchsia::gpu::magma::Resource> resources,
      ::fidl::VectorView<uint64_t> wait_semaphores, ::fidl::VectorView<uint64_t> signal_semaphores);
  magma_status_t ExecuteImmediateCommands(uint32_t context_id,
                                          ::fidl::VectorView<uint8_t> command_data,
                                          ::fidl::VectorView<uint64_t> semaphores);
  magma_status_t MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset,
                              uint64_t page_count, uint64_t flags);
  magma_status_t UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va);
  magma_status_t CommitBuffer(uint64_t buffer_id, uint64_t page_offset, uint64_t page_count);
  magma_status_t BufferRangeOp(uint64_t buffer_id, llcpp::fuchsia::gpu::magma::BufferOp op,
                               uint64_t start, uint64_t length);
  magma_status_t AccessPerformanceCounters(zx::event event);
  magma_status_t EnablePerformanceCounters(fidl::VectorView<uint64_t> counters);
  magma_status_t CreatePerformanceCounterBufferPool(uint64_t pool_id, zx::channel event_channel);
  magma_status_t ReleasePerformanceCounterBufferPool(uint64_t pool_id);
  magma_status_t AddPerformanceCounterBufferOffsetsToPool(
      uint64_t pool_id, fidl::VectorView<llcpp::fuchsia::gpu::magma::BufferOffset> offsets);
  magma_status_t RemovePerformanceCounterBufferFromPool(uint64_t pool_id, uint64_t buffer_id);
  magma_status_t DumpPerformanceCounters(uint64_t pool_id, uint32_t trigger_id);
  magma_status_t ClearPerformanceCounters(fidl::VectorView<uint64_t> counters);

  // Skipped for GetError
  auto GetError() { return client_.GetError(); }
  auto IsPerformanceCounterAccessEnabled() { return client_.IsPerformanceCounterAccessEnabled(); }

  // Returns: bool wait, uint64_t message count, uint64_t imported bytes
  std::tuple<bool, uint64_t, uint64_t> ShouldWait(uint64_t new_bytes);

  void set_for_test(uint64_t inflight_count, uint64_t inflight_bytes) {
    inflight_count_ = inflight_count;
    inflight_bytes_ = inflight_bytes;
  }

  uint64_t inflight_count() { return inflight_count_; }
  uint64_t inflight_bytes() { return inflight_bytes_; }

 private:
  void FlowControl(uint64_t new_bytes = 0) MAGMA_REQUIRES(flow_control_mutex_);
  void UpdateFlowControl(uint64_t new_bytes = 0) MAGMA_REQUIRES(flow_control_mutex_);

  llcpp::fuchsia::gpu::magma::Primary::SyncClient client_;
  const uint64_t max_inflight_messages_;
  const uint64_t max_inflight_bytes_;
  bool flow_control_enabled_ = false;
  uint64_t inflight_count_ = 0;
  uint64_t inflight_bytes_ = 0;
  std::mutex flow_control_mutex_;
};

}  // namespace magma

#endif  // ZIRCON_PLATFORM_CONNECTION_CLIENT_H
