// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_FRAME_MANAGER_H_
#define LIB_ESCHER_IMPL_FRAME_MANAGER_H_

#include <queue>

#include "lib/escher/forward_declarations.h"
#include "lib/escher/impl/uniform_buffer_pool.h"
#include "lib/escher/resources/resource_manager.h"
#include "lib/escher/vk/command_buffer.h"

namespace escher {
namespace impl {

// Responsible for allocating new Frames and (eventually) intelligently
// recycling their per-frame memory allocations.
class FrameManager : public ResourceManager {
 public:
  explicit FrameManager(EscherWeakPtr escher);
  ~FrameManager();

  FramePtr NewFrame(const char* trace_literal, uint64_t frame_number,
                    bool enable_gpu_logging,
                    escher::CommandBuffer::Type requested_type);

  // Return the number of outstanding frames: frames where BeginFrame() has been
  // called, and either EndFrame() not yet called, or called but Vulkan has not
  // yet finished rendering the frame.
  //
  // This value is not used internally within Escher; it is provided as a
  // convenience to clients.
  uint32_t num_outstanding_frames() const { return num_outstanding_frames_; }

  // OK to be public, since FrameManager is not exposed by Escher.
  void IncrementNumOutstandingFrames() { ++num_outstanding_frames_; }
  void DecrementNumOutstandingFrames() { --num_outstanding_frames_; }

 private:
  // |Owner::OnReceiveOwnable()|
  void OnReceiveOwnable(std::unique_ptr<Resource> resource) override;

  // Return a free BlockAllocator from |block_allocators_|, or a new one if none
  // are free.
  std::unique_ptr<BlockAllocator> GetBlockAllocator();

  std::queue<std::unique_ptr<BlockAllocator>> block_allocators_;

  uint32_t num_outstanding_frames_ = 0;
  UniformBufferPool uniform_buffer_pool_;
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_FRAME_MANAGER_H_
