// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/engine/frame_timings.h"

#include "garnet/bin/ui/scene_manager/engine/frame_scheduler.h"

namespace scene_manager {

FrameTimings::FrameTimings() : FrameTimings(nullptr, 0, 0) {}

FrameTimings::FrameTimings(FrameScheduler* frame_scheduler,
                           uint64_t frame_number,
                           mx_time_t target_presentation_time)
    : frame_scheduler_(frame_scheduler),
      frame_number_(frame_number),
      target_presentation_time_(target_presentation_time) {}

size_t FrameTimings::AddSwapchain(Swapchain* swapchain) {
  // All swapchains that we are timing must be added before any of them finish.
  // The purpose of this is to verify that we cannot notify the FrameScheduler
  // that the frame has finished before all swapchains have been added.
  FTL_DCHECK(frame_finished_rendering_count_ == 0);
  FTL_DCHECK(frame_presented_count_ == 0);
  swapchain_records_.push_back({});
  return swapchain_records_.size() - 1;
}

void FrameTimings::OnFrameFinishedRendering(size_t swapchain_index,
                                            mx_time_t time) {
  FTL_DCHECK(swapchain_index < swapchain_records_.size());
  FTL_DCHECK(frame_finished_rendering_count_ < swapchain_records_.size());
  FTL_DCHECK(swapchain_records_[swapchain_index].frame_finished_time == 0);
  FTL_DCHECK(time > 0);
  swapchain_records_[swapchain_index].frame_finished_time = time;

  ++frame_finished_rendering_count_;
  if (frame_finished_rendering_count_ == swapchain_records_.size() &&
      frame_presented_count_ == swapchain_records_.size()) {
    Finalize();
  }
}

void FrameTimings::OnFramePresented(size_t swapchain_index, mx_time_t time) {
  FTL_DCHECK(swapchain_index < swapchain_records_.size());
  FTL_DCHECK(frame_presented_count_ < swapchain_records_.size());
  FTL_DCHECK(swapchain_records_[swapchain_index].frame_presented_time == 0);
  FTL_DCHECK(time > 0);
  swapchain_records_[swapchain_index].frame_presented_time = time;

  ++frame_presented_count_;
  if (frame_finished_rendering_count_ == swapchain_records_.size() &&
      frame_presented_count_ == swapchain_records_.size()) {
    Finalize();
  }
}

void FrameTimings::Finalize() {
  // TODO: compute actual presentation time.
  FTL_CHECK(false);

  frame_scheduler_->ReceiveFrameTimings(this);
}

}  // namespace scene_manager
