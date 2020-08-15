// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMMAND_BUFFER_H
#define COMMAND_BUFFER_H

#include <magma_util/accessor.h>
#include <magma_util/command_buffer.h>

#include "gpu_mapping.h"
#include "instructions.h"
#include "msd_vsi_context.h"

class CommandBuffer : public magma::CommandBuffer<MsdVsiContext, GpuMapping> {
 public:
  // The client is required to provide a buffer with at least 8 additional bytes available
  // and mapped, which the driver will write a LINK instruction in.
  static constexpr uint32_t kAdditionalBytes = kInstructionDwords * sizeof(uint32_t);

  // Only up to 2 resources are supported, the batch buffer and optional context state buffer.
  static constexpr uint32_t kMaxAllowedResources = 2;

  static std::unique_ptr<CommandBuffer> Create(
      std::shared_ptr<MsdVsiContext> context, msd_client_id_t client_id,
      std::unique_ptr<magma_system_command_buffer> cmd_buf, std::vector<ExecResource> resources,
      std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores) {
    if (cmd_buf->resource_count > kMaxAllowedResources) {
      return DRETP(nullptr, "Invalid resource count, only 1 additional context state is supported");
    }
    std::optional<uint32_t> context_state_buffer_resource_index;
    if (cmd_buf->resource_count == 2) {
      context_state_buffer_resource_index = cmd_buf->batch_buffer_resource_index == 0 ? 1 : 0;
    }
    auto command_buffer = std::make_unique<CommandBuffer>(context, client_id, std::move(cmd_buf),
                                                          context_state_buffer_resource_index);
    if (!command_buffer->InitializeResources(std::move(resources), {} /* wait_semaphores */,
                                             std::move(signal_semaphores))) {
      return DRETP(nullptr, "Failed to initialize resources");
    }
    return command_buffer;
  }

  CommandBuffer(std::weak_ptr<MsdVsiContext> context, uint64_t connection_id,
                std::unique_ptr<magma_system_command_buffer> command_buffer,
                std::optional<uint32_t> csb_resource_index = std::nullopt)
      : magma::CommandBuffer<MsdVsiContext, GpuMapping>(context, connection_id,
                                                        std::move(command_buffer)),
        csb_index_(csb_resource_index) {}

  // Returns a pointer to the batch buffer.
  magma::PlatformBuffer* GetBatchBuffer() {
    if (batch_buffer_index() < exec_resources_.size()) {
      return exec_resources_[batch_buffer_index()].buffer->platform_buffer();
    }
    DASSERT(false);
    return nullptr;
  }

  // Returns the offset into the batch buffer that points to the end of the user data.
  uint32_t GetBatchBufferWriteOffset() {
    uint32_t length = magma::round_up(GetLength(), sizeof(uint64_t));
    return batch_start_offset() + length;
  }

  // Returns a pointer to the resource for the context state buffer.
  // May be null if no context state buffer is present.
  const ExecResource* GetContextStateBufferResource() const {
    if (!csb_index_.has_value()) {
      return nullptr;
    }
    auto index = csb_index_.value();
    DASSERT(index < exec_resources_.size());
    return &exec_resources_[index];
  }

  // Returns a read only view of the context state buffer's GPU mapping.
  // May be null if no context state buffer is present.
  const GpuMappingView* GetContextStateBufferMapping() const {
    DASSERT(prepared_to_execute_);
    if (!csb_index_.has_value()) {
      return nullptr;
    }
    auto index = csb_index_.value();
    DASSERT(index < exec_resource_mappings_.size());
    return exec_resource_mappings_[index].get();
  }

  // Returns whether the batch buffer and context state buffer (if present) are valid.
  // This should only be called after |PrepareForExecution|.
  bool IsValidBatch() {
    DASSERT(prepared_to_execute_);

    if ((batch_start_offset() & (sizeof(uint64_t) - 1)) != 0) {
      return DRETF(false, "batch start offset is not 8 byte aligned");
    }
    auto mapping = GetBatchMapping();
    // |GetLength| returns the actual size of the user's data.
    if (mapping->length() < batch_start_offset() + GetLength() + kAdditionalBytes) {
      return DRETF(false, "insufficient space for LINK command, mapped %lu used %lu need %u\n",
                   mapping->length(), GetLength(), kAdditionalBytes);
    }

    auto csb = GetContextStateBufferResource();
    if (csb) {
      // Check that the mapped length can fit the user data and also an additional LINK command.
      auto csb_mapping = GetContextStateBufferMapping();
      DASSERT(csb_mapping);
      if (csb_mapping->length() < csb->length + kAdditionalBytes) {
        return DRETF(false,
                     "CSB: insufficient space for LINK command, mapped %lu used %lu need %u\n",
                     csb_mapping->length(), csb->length, kAdditionalBytes);
      }
    }
    return true;
  }

 private:
  std::optional<uint32_t> csb_index_;
};

#endif  // COMMAND_BUFFER_H
