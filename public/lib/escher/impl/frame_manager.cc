// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/frame_manager.h"

#include "lib/escher/renderer/frame.h"
#include "lib/escher/util/trace_macros.h"

namespace escher {
namespace impl {

FrameManager::FrameManager(EscherWeakPtr escher)
    : ResourceManager(escher),
      // TODO(ES-103): the intention here was to use UniformBufferPool's
      // recently-added ring-based recycling to manage resource reclamation.
      // However, this would conflict with upcoming GpuUploader changes; see
      // SCN-842.
      // For now, clients must use an approach like ModelDisplayListBuilder's,
      // which takes additional steps to retain uniform buffers to prevent them
      // from being returned to the pool too early, resulting in the current
      // frame's data being stomped by the next frame's while it is still in
      // use.
      uniform_buffer_pool_(std::move(escher), 1) {
  // Escher apps will at least double-buffer, so avoid expensive allocations in
  // the initial few frames.
  constexpr int kInitialBlockAllocatorCount = 2;
  for (int i = 0; i < kInitialBlockAllocatorCount; ++i) {
    block_allocators_.push(std::make_unique<BlockAllocator>());
  }
}

FrameManager::~FrameManager() = default;

FramePtr FrameManager::NewFrame(const char* trace_literal,
                                uint64_t frame_number, bool enable_gpu_logging,
                                escher::CommandBuffer::Type requested_type) {
  TRACE_DURATION("gfx", "escher::FrameManager::NewFrame");
  uniform_buffer_pool_.BeginFrame();
  FramePtr frame = fxl::AdoptRef<Frame>(
      new Frame(this, requested_type, std::move(*GetBlockAllocator().get()),
                uniform_buffer_pool_.GetWeakPtr(), frame_number, trace_literal,
                enable_gpu_logging));
  frame->BeginFrame();
  return frame;
}

void FrameManager::OnReceiveOwnable(std::unique_ptr<Resource> resource) {
  FXL_DCHECK(resource->IsKindOf<Frame>());
  auto frame = static_cast<Frame*>(resource.get());
  block_allocators_.push(
      std::make_unique<BlockAllocator>(frame->TakeBlockAllocator()));
}

std::unique_ptr<BlockAllocator> FrameManager::GetBlockAllocator() {
  if (block_allocators_.empty()) {
    return std::make_unique<BlockAllocator>();
  }

  std::unique_ptr<BlockAllocator> alloc(std::move(block_allocators_.front()));
  block_allocators_.pop();
  return alloc;
}

}  // namespace impl
}  // namespace escher
