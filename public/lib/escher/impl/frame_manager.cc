// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/frame_manager.h"

#include "lib/escher/renderer/frame.h"

namespace escher {
namespace impl {

FrameManager::FrameManager(EscherWeakPtr escher)
    : ResourceManager(std::move(escher)) {}

FramePtr FrameManager::NewFrame(const char* trace_literal,
                                uint64_t frame_number,
                                bool enable_gpu_logging) {
  auto frame = fxl::AdoptRef<Frame>(
      new Frame(this, frame_number, trace_literal, enable_gpu_logging));
  frame->BeginFrame();
  return frame;
}

void FrameManager::OnReceiveOwnable(std::unique_ptr<Resource> resource) {
  FXL_DCHECK(resource->IsKindOf<Frame>());
}

}  // namespace impl
}  // namespace escher
