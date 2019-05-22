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
    zx_time_t presentation_time, uint64_t trace_id) {
  ++update_sessions_call_count_;
  return update_sessions_return_value_;
}

bool MockFrameRenderer::RenderFrame(const FrameTimingsPtr& frame_timings,
                                    zx_time_t presentation_time) {
  const uint64_t frame_number = frame_timings->frame_number();
  FXL_DCHECK(frames_.find(frame_number) == frames_.end());
  // Check that no frame numbers were skipped.
  FXL_CHECK(frame_number == last_frame_number_ + 1);
  last_frame_number_ = frame_number;

  ++render_frame_call_count_;
  size_t swapchain_index = frame_timings->RegisterSwapchain();
  frames_[frame_number] = {.frame_timings = std::move(frame_timings),
                           .swapchain_index = swapchain_index};
  return render_frame_return_value_;
}

void MockFrameRenderer::EndFrame(uint64_t frame_number, zx_time_t time_done) {
  SignalFrameRendered(frame_number, time_done);
  SignalFramePresented(frame_number, time_done);
}

void MockFrameRenderer::SignalFrameRendered(uint64_t frame_number,
                                            zx_time_t time_done) {
  auto find_it = frames_.find(frame_number);
  FXL_DCHECK(find_it != frames_.end());

  auto& frame = find_it->second;
  if (!frame.frame_rendered) {
    frame.frame_rendered = true;
    frame.frame_timings->OnFrameRendered(frame.swapchain_index, time_done);
  }
  CleanUpFrame(frame_number);
}

void MockFrameRenderer::SignalFramePresented(uint64_t frame_number,
                                             zx_time_t time_done) {
  auto find_it = frames_.find(frame_number);
  FXL_DCHECK(find_it != frames_.end());

  auto& frame = find_it->second;
  if (!frame.frame_presented) {
    frame.frame_presented = true;
    frame.frame_timings->OnFramePresented(frame.swapchain_index, time_done);
  }
  CleanUpFrame(frame_number);
}

void MockFrameRenderer::SignalFrameDropped(uint64_t frame_number) {
  auto find_it = frames_.find(frame_number);
  FXL_DCHECK(find_it != frames_.end());

  auto& frame = find_it->second;
  if (!frame.frame_presented) {
    frame.frame_presented = true;
    frame.frame_timings->OnFrameDropped(frame.swapchain_index);
  }
  CleanUpFrame(frame_number);
}

void MockFrameRenderer::CleanUpFrame(uint64_t frame_number) {
  auto find_it = frames_.find(frame_number);
  FXL_DCHECK(find_it != frames_.end());

  auto& frame = find_it->second;
  if (!frame.frame_rendered || !frame.frame_presented) {
    return;
  }
  frames_.erase(frame_number);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
