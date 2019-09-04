// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_IMPL_FRAME_MANAGER_H_
#define SRC_UI_LIB_ESCHER_IMPL_FRAME_MANAGER_H_

#include <queue>

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/impl/uniform_buffer_pool.h"
#include "src/ui/lib/escher/resources/resource_manager.h"
#include "src/ui/lib/escher/vk/command_buffer.h"

namespace escher {
namespace impl {

// Responsible for allocating new Frames and (eventually) intelligently
// recycling their per-frame memory allocations.
class FrameManager : public ResourceManager {
 public:
  explicit FrameManager(EscherWeakPtr escher);
  ~FrameManager();

  FramePtr NewFrame(const char* trace_literal, uint64_t frame_number, bool enable_gpu_logging,
                    escher::CommandBuffer::Type requested_type);

 private:
  // |Owner::OnReceiveOwnable()|
  void OnReceiveOwnable(std::unique_ptr<Resource> resource) override;

  // Return a free BlockAllocator from |block_allocators_|, or a new one if none
  // are free.
  std::unique_ptr<BlockAllocator> GetBlockAllocator();

  std::queue<std::unique_ptr<BlockAllocator>> block_allocators_;

  UniformBufferPool uniform_buffer_pool_;

  // The name of the default vthread for GPU trace events. This should be a
  // string literal.
  const char* gpu_vthread_literal_;
  // The default vthread ID for GPU trace events.
  const uint64_t gpu_vthread_id_;
};

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_IMPL_FRAME_MANAGER_H_
