// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_COMMAND_BUFFER_H_
#define SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_COMMAND_BUFFER_H_

#include <magma_common_defs.h>
#include <platform_semaphore.h>
#include <platform_trace.h>

#include <memory>
#include <vector>

#include "accessor.h"
#include "macros.h"
#include "mapped_batch.h"

namespace magma {

// CommandBuffer is initialized with resources (buffers), wait semaphores (must be signaled prior
// to execution), and signal semaphores (signaled after execution completes).  References to GPU
// mappings of buffer resources are retained for the lifetime of the CommandBuffer.
template <typename Context, typename GpuMapping>
class CommandBuffer : public MappedBatch<Context, typename GpuMapping::BufferType> {
 public:
  using Buffer = typename GpuMapping::BufferType;
  using GpuMappingView = GpuMappingView<Buffer>;

  CommandBuffer(std::weak_ptr<Context> context, uint64_t connection_id,
                std::unique_ptr<magma_system_command_buffer> command_buffer)
      : context_(context),
        command_buffer_(std::move(command_buffer)),
        connection_id_(connection_id),
        nonce_(TRACE_NONCE()) {}

  ~CommandBuffer();

  bool IsCommandBuffer() const override { return true; }

  std::weak_ptr<Context> GetContext() const override { return context_; }

  void SetSequenceNumber(uint32_t sequence_number) override {
    TRACE_ASYNC_BEGIN("magma-exec", "CommandBuffer Exec", nonce_, "id", GetBatchBufferId());
    sequence_number_ = sequence_number;
  }

  uint32_t GetSequenceNumber() const override { return sequence_number_; }

  // A buffer and a region, used to initialize resources.
  struct ExecResource {
    std::shared_ptr<Buffer> buffer;
    uint64_t offset;
    uint64_t length;
  };

  // Initializes the command buffer with the given resources and semaphores.  The number of
  // resources and semaphores given here must match the sizes passed in the
  // magma_system_command_buffer at construction. Wait semaphores are held but not otherwise used.
  // Signal semaphores are signaled when the CommandBuffer is destroyed.
  bool InitializeResources(
      std::vector<ExecResource> resources,
      std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores,
      std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores);

  // Returns the length of the batch buffer.
  uint64_t GetLength() const override {
    if (batch_buffer_index() < exec_resources_.size())
      return exec_resources_[batch_buffer_index()].length;
    DASSERT(false);
    return 0;
  }

  // Returns the ID of the batch buffer.
  uint64_t GetBatchBufferId() const override {
    if (batch_buffer_index() < exec_resources_.size())
      return BufferAccessor<Buffer>::platform_buffer(
                 exec_resources_[batch_buffer_index()].buffer.get())
          ->id();
    DASSERT(false);
    return 0;
  }

  // Prepare the command buffer for execution.  This will look in the context's exec address space
  // for GPU mappings corresponding to each of the exec resources, and retain references to those
  // mappings until the CommandBuffer is destroyed.
  bool PrepareForExecution();

  // Returns the GPU address of the batch buffer.
  uint64_t GetGpuAddress() const override {
    DASSERT(prepared_to_execute_);
    return exec_resource_mappings_[batch_buffer_index()]->gpu_addr() + batch_start_offset();
  }

  // Returns a read only view of the batch buffer's GPU mapping.
  const GpuMappingView* GetBatchMapping() const override {
    DASSERT(prepared_to_execute_);
    return exec_resource_mappings_[batch_buffer_index()].get();
  }

  // Takes ownership of the wait semaphores array
  std::vector<std::shared_ptr<magma::PlatformSemaphore>> TakeWaitSemaphores() {
    return std::move(wait_semaphores_);
  }

  // Returns read only views for all GPU mappings.
  void GetMappings(std::vector<GpuMappingView*>* mappings_out) {
    mappings_out->clear();
    for (const auto& mapping : exec_resource_mappings_) {
      mappings_out->emplace_back(mapping.get());
    }
  }

 protected:
  uint32_t batch_buffer_index() const { return command_buffer_->batch_buffer_resource_index; }

  uint64_t batch_start_offset() const { return command_buffer_->batch_start_offset; }

  uint32_t num_resources() const { return command_buffer_->resource_count; }

  uint32_t wait_semaphore_count() const { return command_buffer_->wait_semaphore_count; }

  uint32_t signal_semaphore_count() const { return command_buffer_->signal_semaphore_count; }

  bool MapResourcesGpu(std::shared_ptr<AddressSpace<GpuMapping>> address_space,
                       std::vector<std::shared_ptr<GpuMapping>>& mappings);

  void UnmapResourcesGpu() { exec_resource_mappings_.clear(); }

  const std::weak_ptr<Context> context_;
  const std::unique_ptr<magma_system_command_buffer> command_buffer_;
  const uint64_t connection_id_;
  const uint64_t nonce_;

  // Initialized on connection thread via PrepareForExecution; read only afterward.
  bool prepared_to_execute_ = false;
  // Valid only when prepared_to_execute_ is true.
  std::vector<ExecResource> exec_resources_;
  std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores_;
  std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores_;
  std::vector<std::shared_ptr<GpuMapping>> exec_resource_mappings_;
  std::shared_ptr<Context> locked_context_;

  // Set on device thread via SetSequenceNumber.
  uint32_t sequence_number_ = 0;
};

template <typename Context, typename GpuMapping>
bool CommandBuffer<Context, GpuMapping>::InitializeResources(
    std::vector<ExecResource> resources,
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores,
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores) {
  TRACE_DURATION("magma", "InitializeResources");

  if (num_resources() != resources.size())
    return DRETF(false, "resources size mismatch");

  if (wait_semaphores.size() != wait_semaphore_count())
    return DRETF(false, "wait semaphore count mismatch");

  if (signal_semaphores.size() != signal_semaphore_count())
    return DRETF(false, "wait semaphore count mismatch");

  exec_resources_ = std::move(resources);
  wait_semaphores_ = std::move(wait_semaphores);
  signal_semaphores_ = std::move(signal_semaphores);

  return true;
}

template <typename Context, typename GpuMapping>
bool CommandBuffer<Context, GpuMapping>::PrepareForExecution() {
  locked_context_ = context_.lock();
  if (!locked_context_)
    return DRETF(false, "context has already been deleted, aborting");

  exec_resource_mappings_.clear();
  exec_resource_mappings_.reserve(exec_resources_.size());

  TRACE_FLOW_STEP("magma", "command_buffer", GetBatchBufferId());

  if (!MapResourcesGpu(ContextAccessor<Context, AddressSpace<GpuMapping>>::exec_address_space(
                           locked_context_.get()),
                       exec_resource_mappings_))
    return DRETF(false, "failed to map execution resources");

  prepared_to_execute_ = true;

  return true;
}

template <typename Context, typename GpuMapping>
bool CommandBuffer<Context, GpuMapping>::MapResourcesGpu(
    std::shared_ptr<AddressSpace<GpuMapping>> address_space,
    std::vector<std::shared_ptr<GpuMapping>>& mappings) {
  TRACE_DURATION("magma", "MapResourcesGpu");

  for (auto res : exec_resources_) {
    magma::PlatformBuffer* platform_buffer =
        BufferAccessor<Buffer>::platform_buffer(res.buffer.get());
    std::shared_ptr<GpuMapping> mapping =
        address_space->FindGpuMapping(platform_buffer, res.offset, res.length);
    if (!mapping)
      return DRETF(false, "failed to find gpu mapping for buffer %lu", platform_buffer->id());
    DLOG("MapResourcesGpu aspace %p buffer 0x%" PRIx64 " offset 0x%" PRIx64 " length 0x%" PRIx64
         " gpu_addr 0x%" PRIx64,
         address_space.get(), platform_buffer->id(), res.offset, res.length, mapping->gpu_addr());
    mappings.push_back(mapping);
  }

  return true;
}

template <typename Context, typename GpuMapping>
CommandBuffer<Context, GpuMapping>::~CommandBuffer() {
  if (!prepared_to_execute_)
    return;

  uint64_t ATTRIBUTE_UNUSED current_ticks = magma::PlatformTrace::GetCurrentTicks();
  uint64_t ATTRIBUTE_UNUSED buffer_id = GetBatchBufferId();

  TRACE_DURATION("magma", "Command Buffer End");
  TRACE_VTHREAD_FLOW_STEP("magma", "command_buffer", "GPU", connection_id_, buffer_id,
                          current_ticks);
  TRACE_FLOW_END("magma", "command_buffer", buffer_id);

  UnmapResourcesGpu();

  for (auto& semaphore : signal_semaphores_) {
    semaphore->Signal();
  }

  TRACE_ASYNC_END("magma-exec", "CommandBuffer Exec", nonce_);
}

}  // namespace magma

#endif  // SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_COMMAND_BUFFER_H_
