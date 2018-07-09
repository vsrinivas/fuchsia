// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_FRAME_MANAGER_H_
#define LIB_ESCHER_IMPL_FRAME_MANAGER_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/resources/resource_manager.h"

namespace escher {
namespace impl {

// Responsible for allocating new Frames and (eventually) intelligently
// recycling their per-frame memory allocations.
class FrameManager : public ResourceManager {
 public:
  explicit FrameManager(EscherWeakPtr escher);

  FramePtr NewFrame(const char* trace_literal, uint64_t frame_number,
                    bool enable_gpu_logging);

  uint32_t num_outstanding_frames() const { return num_outstanding_frames_; }

  // OK to be public, since FrameManager is not exposed by Escher.
  void IncrementNumOutstandingFrames() { ++num_outstanding_frames_; }
  void DecrementNumOutstandingFrames() { --num_outstanding_frames_; }

 private:
  // |Owner::OnReceiveOwnable()|
  void OnReceiveOwnable(std::unique_ptr<Resource> resource) override;

  uint32_t num_outstanding_frames_ = 0;
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_FRAME_MANAGER_H_
