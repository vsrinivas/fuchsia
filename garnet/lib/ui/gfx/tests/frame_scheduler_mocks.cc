// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/frame_scheduler_mocks.h"

namespace scenic_impl {
namespace gfx {
namespace test {

bool MockFrameRenderer::RenderFrame(const FrameTimingsPtr& frame_timings,
                                    zx_time_t presentation_time,
                                    zx_duration_t presentation_interval) {
  // Check that no frame numbers were skipped.
  FXL_CHECK(frame_timings->frame_number() == last_frame_number_ + 1);
  last_frame_number_ = frame_timings->frame_number();

  ++render_frame_call_count_;
  frame_timings->AddSwapchain(nullptr);
  frames_.emplace_back(frame_timings.get());
  return render_frame_return_value_;
}

void MockFrameRenderer::EndFrame(size_t frame_index) {
  if (frame_index >= frames_.size()) {
    FXL_LOG(WARNING) << "Frame index out of bounds";
    return;
  }

  auto& frame = frames_[frame_index];

  if (!frame.frame_rendered) {
    SignalFrameRendered(frame_index);
  }

  if (!frame.frame_presented) {
    SignalFramePresented(frame_index);
  }

  frames_.erase(frames_.begin() + frame_index);
}

void MockFrameRenderer::SignalFrameRendered(size_t frame_index) {
  if (frame_index >= frames_.size()) {
    FXL_LOG(WARNING) << "Frame index out of bounds";
    return;
  }

  auto& frame = frames_[frame_index];
  if (!frame.frame_rendered) {
    frame.frame_timings->OnFrameRendered(/*swapchain index*/ 0, /*time*/ 1);
    frame.frame_rendered = true;
  }
}

void MockFrameRenderer::SignalFramePresented(size_t frame_index) {
  if (frame_index >= frames_.size()) {
    FXL_LOG(WARNING) << "Frame index out of bounds";
    return;
  }

  auto& frame = frames_[frame_index];
  if (!frame.frame_presented) {
    frame.frame_timings->OnFramePresented(/*swapchain index*/ 0, /*time*/ 1);
    frame.frame_presented = true;
  }
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
