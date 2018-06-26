// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/frame_timings.h"

#include "garnet/lib/ui/gfx/engine/frame_scheduler.h"

namespace scenic {
namespace gfx {

FrameTimings::FrameTimings() : FrameTimings(nullptr, 0, 0) {}

FrameTimings::FrameTimings(FrameScheduler* frame_scheduler,
                           uint64_t frame_number,
                           zx_time_t target_presentation_time)
    : frame_scheduler_(frame_scheduler),
      frame_number_(frame_number),
      target_presentation_time_(target_presentation_time) {}

size_t FrameTimings::AddSwapchain(Swapchain* swapchain) {
  // All swapchains that we are timing must be added before any of them finish.
  // The purpose of this is to verify that we cannot notify the FrameScheduler
  // that the frame has finished before all swapchains have been added.
  FXL_DCHECK(frame_rendered_count_ == 0);
  FXL_DCHECK(frame_presented_count_ == 0);
  swapchain_records_.push_back({});
  return swapchain_records_.size() - 1;
}

void FrameTimings::OnFrameRendered(size_t swapchain_index, zx_time_t time) {
  FXL_DCHECK(swapchain_index < swapchain_records_.size());
  FXL_DCHECK(frame_rendered_count_ < swapchain_records_.size());
  FXL_DCHECK(time > 0);

  auto& record = swapchain_records_[swapchain_index];
  FXL_DCHECK(swapchain_records_[swapchain_index].frame_rendered_time == 0);

  if (record.frame_presented_time > 0 && record.frame_presented_time < time) {
    // NOTE: Because there is a delay between when rendering is actually
    // completed and when EventTimestamper generates the timestamp, it's
    // possible that this timestamp is later than the present timestamp. Since
    // we know that's actually impossible, adjust the render timestamp to
    // make it a bit more accurate.
    time = record.frame_presented_time;
  }

  swapchain_records_[swapchain_index].frame_rendered_time = time;

  ++frame_rendered_count_;
  if (received_all_callbacks()) {
    Finalize();
  }
}

void FrameTimings::OnFramePresented(size_t swapchain_index, zx_time_t time) {
  FXL_DCHECK(swapchain_index < swapchain_records_.size());
  FXL_DCHECK(frame_presented_count_ < swapchain_records_.size());
  FXL_DCHECK(swapchain_records_[swapchain_index].frame_presented_time == 0);
  FXL_DCHECK(time > 0);
  swapchain_records_[swapchain_index].frame_presented_time = time;

  if (time > actual_presentation_time_) {
    actual_presentation_time_ = time;
  }

  ++frame_presented_count_;
  if (received_all_callbacks()) {
    Finalize();
  }
}

void FrameTimings::Finalize() {
  FXL_DCHECK(!finalized());
  finalized_ = true;

  if (frame_scheduler_) {
    frame_scheduler_->OnFramePresented(this);
  }
}

}  // namespace gfx
}  // namespace scenic
