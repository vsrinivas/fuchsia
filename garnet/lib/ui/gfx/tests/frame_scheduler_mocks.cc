// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/frame_scheduler_mocks.h"

#include <lib/gtest/test_loop_fixture.h>

namespace scenic_impl {
namespace gfx {
namespace test {

SessionUpdater::UpdateResults MockSessionUpdater::UpdateSessions(
    std::unordered_set<SessionId> sessions_to_update,
    zx_time_t presentation_time) {
  ++update_sessions_call_count_;
  return update_sessions_return_value_;
}

bool MockFrameRenderer::RenderFrame(const FrameTimingsPtr& frame_timings,
                                    zx_time_t presentation_time) {
  // Check that no frame numbers were skipped.
  FXL_CHECK(frame_timings->frame_number() == last_frame_number_ + 1);
  last_frame_number_ = frame_timings->frame_number();

  ++render_frame_call_count_;
  frame_timings->AddSwapchain(nullptr);
  frames_.push_back({.frame_timings = std::move(frame_timings)});
  return render_frame_return_value_;
}

void MockFrameRenderer::EndFrame(size_t frame_index) {
  SignalFrameRendered(frame_index);
  SignalFramePresented(frame_index);
}

void MockFrameRenderer::SignalFrameRendered(size_t frame_index) {
  FXL_DCHECK(frame_index < frames_.size());
  auto& frame = frames_[frame_index];
  if (!frame.frame_rendered) {
    frame.frame_rendered = true;
    frame.frame_timings->OnFrameRendered(/*swapchain index*/ 0, /*time*/ 1);
  }
}

void MockFrameRenderer::SignalFramePresented(size_t frame_index) {
  FXL_DCHECK(frame_index < frames_.size());
  auto& frame = frames_[frame_index];
  frame.frame_timings->OnFramePresented(/*swapchain index*/ 0, /*time*/ 1);
  frames_.erase(frames_.begin() + frame_index);
}

void MockFrameRenderer::SignalFrameDropped(size_t frame_index) {
  FXL_DCHECK(frame_index < frames_.size());
  auto& frame = frames_[frame_index];
  frame.frame_timings->OnFrameDropped(/*swapchain index*/ 0);
  frames_.erase(frames_.begin() + frame_index);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
