// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMMAND_BUFFER_H
#define COMMAND_BUFFER_H

#include <magma_util/accessor.h>
#include <magma_util/command_buffer.h>

#include "gpu_mapping.h"
#include "instructions.h"
#include "msd_vsl_context.h"

class CommandBuffer : public magma::CommandBuffer<MsdVslContext, GpuMapping> {
 public:
  // The client is required to provide a buffer with at least 8 additional bytes available
  // and mapped, which the driver will write a LINK instruction in.
  static constexpr uint32_t kAdditionalBytes = kInstructionDwords * sizeof(uint32_t);

  CommandBuffer(std::weak_ptr<MsdVslContext> context, uint64_t connection_id,
                std::unique_ptr<magma_system_command_buffer> command_buffer)
      : magma::CommandBuffer<MsdVslContext, GpuMapping>(context, connection_id,
                                                        std::move(command_buffer)) {}

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

  // Returns whether the batch buffer is correctly aligned and provides the required
  // |kAdditionalBytes|.
  // This should only be called after |PrepareForExecution|.
  bool IsValidBatchBuffer() {
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
    return true;
  }
};

#endif  // COMMAND_BUFFER_H
