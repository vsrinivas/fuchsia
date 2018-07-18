// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/frame_manager.h"

#include "lib/escher/renderer/frame.h"

#include "lib/escher/util/trace_macros.h"

namespace escher {
namespace impl {

FrameManager::FrameManager(EscherWeakPtr escher)
    : ResourceManager(std::move(escher)) {
  // Escher apps will at least double-buffer, so avoid expensive allocations in
  // the initial few frames.
  constexpr int kInitialBlockAllocatorCount = 2;
  for (int i = 0; i < kInitialBlockAllocatorCount; ++i) {
    block_allocators_.push(std::make_unique<BlockAllocator>());
  }
}

FrameManager::~FrameManager() = default;

FramePtr FrameManager::NewFrame(const char* trace_literal,
                                uint64_t frame_number,
                                bool enable_gpu_logging) {
  TRACE_DURATION("gfx", "escher::FrameManager::NewFrame");
  FramePtr frame = fxl::AdoptRef<Frame>(
      new Frame(this, std::move(*GetBlockAllocator().get()), frame_number,
                trace_literal, enable_gpu_logging));
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
