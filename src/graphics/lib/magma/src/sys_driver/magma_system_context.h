// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_SRC_SYS_DRIVER_MAGMA_SYSTEM_CONTEXT_H_
#define SRC_GRAPHICS_LIB_MAGMA_SRC_SYS_DRIVER_MAGMA_SYSTEM_CONTEXT_H_

#include <functional>
#include <memory>

#include "magma_system_buffer.h"
#include "magma_system_semaphore.h"
#include "magma_util/status.h"
#include "msd.h"

using msd_context_unique_ptr_t = std::unique_ptr<msd_context_t, decltype(&msd_context_destroy)>;

static inline msd_context_unique_ptr_t MsdContextUniquePtr(msd_context_t* context) {
  return msd_context_unique_ptr_t(context, &msd_context_destroy);
}

class MagmaSystemCommandBuffer;

class MagmaSystemContext {
 public:
  class Owner {
   public:
    virtual std::shared_ptr<MagmaSystemBuffer> LookupBufferForContext(uint64_t id) = 0;
    virtual std::shared_ptr<MagmaSystemSemaphore> LookupSemaphoreForContext(uint64_t id) = 0;
  };

  MagmaSystemContext(Owner* owner, msd_context_unique_ptr_t msd_ctx)
      : owner_(owner), msd_ctx_(std::move(msd_ctx)) {}

  magma::Status ExecuteCommandBufferWithResources(
      std::unique_ptr<magma_system_command_buffer> command_buffer,
      std::vector<magma_system_exec_resource> resources, std::vector<uint64_t> semaphores);
  magma::Status ExecuteImmediateCommands(uint64_t commands_size, void* commands,
                                         uint64_t semaphore_count, uint64_t* semaphore_ids);

 private:
  msd_context_t* msd_ctx() { return msd_ctx_.get(); }

  Owner* owner_;

  msd_context_unique_ptr_t msd_ctx_;

  friend class CommandBufferHelper;
};

#endif  // SRC_GRAPHICS_LIB_MAGMA_SRC_SYS_DRIVER_MAGMA_SYSTEM_CONTEXT_H_
