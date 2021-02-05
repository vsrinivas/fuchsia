// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/swapchain/frame_timings.h"

#include <lib/async/default.h>
#include <lib/async/time.h>

using scheduling::FrameRenderer;

namespace scenic_impl {
namespace gfx {

FrameTimings::FrameTimings(uint64_t frame_number,
                           OnTimingsPresentedCallback timings_presented_callback)
    : frame_number_(frame_number),
      timings_presented_callback_(std::move(timings_presented_callback)) {}

void FrameTimings::RegisterSwapchains(size_t count) {
  // All swapchains that we are timing must be added before any of them finish.
  // The purpose of this is to verify that we cannot notify the FrameScheduler
  // that the frame has finished before all swapchains have been added.
  FX_DCHECK(frame_rendered_count_ == 0);
  FX_DCHECK(frame_presented_count_ == 0);
  FX_DCHECK(actual_presentation_time_ == kTimeUninitialized);
  swapchain_records_.resize(count);
}

void FrameTimings::OnFrameRendered(size_t swapchain_index, zx::time time) {
  FX_DCHECK(swapchain_index < swapchain_records_.size());
  FX_DCHECK(time.get() >= 0);

  auto& record = swapchain_records_[swapchain_index];
  FX_DCHECK(record.frame_rendered_time == kTimeUninitialized)
      << "Frame render time already recorded for swapchain. Render time: "
      << record.frame_rendered_time.get();

  record.frame_rendered_time = time;
  ++frame_rendered_count_;
  if (!received_all_frame_rendered_callbacks()) {
    return;
  }

  // TODO(fxbug.dev/24518): We currently only return the time of the longest received
  // render time. This is not a problem right now, since we only have cases with
  // a single swapchain/display, but need to figure out how to handle the
  // general case.
  //
  // That was the last pending render, compute stats.
  for (const auto& rec : swapchain_records_) {
    if (rec.frame_rendered_time > rendering_finished_time_) {
      rendering_finished_time_ = rec.frame_rendered_time;
    }
  }

  if (received_all_callbacks()) {
    Finalize();
  }
}

void FrameTimings::OnFramePresented(size_t swapchain_index, zx::time time) {
  FX_DCHECK(swapchain_index < swapchain_records_.size());
  FX_DCHECK(frame_presented_count_ < swapchain_records_.size());
  FX_DCHECK(time.get() >= 0);

  auto& record = swapchain_records_[swapchain_index];
  FX_DCHECK(record.frame_presented_time == kTimeUninitialized)
      << "Frame present time already recorded for swapchain. Present time: "
      << record.frame_presented_time.get();

  record.frame_presented_time = time;
  ++frame_presented_count_;
  if (!received_all_frame_presented_callbacks()) {
    return;
  }
  // TODO(fxbug.dev/24518): We currently only return the time of the longest received
  // render time. This is not a problem right now, since we only have cases with
  // a single swapchain/display, but need to figure out how to handle the
  // general case.
  for (const auto& rec : swapchain_records_) {
    if (rec.frame_presented_time > actual_presentation_time_) {
      actual_presentation_time_ = rec.frame_presented_time;
    }
  }

  if (received_all_callbacks()) {
    Finalize();
  }
}

void FrameTimings::OnFrameDropped(size_t swapchain_index) {
  // Indicates that "frame was dropped".
  actual_presentation_time_ = FrameRenderer::kTimeDropped;
  frame_was_dropped_ = true;

  // The record should also reflect that "frame was dropped". Additionally,
  // update counts to simulate calls to OnFrameRendered/OnFramePresented; this
  // maintains count-related invariants.
  auto& record = swapchain_records_[swapchain_index];
  record.frame_presented_time = FrameRenderer::kTimeDropped;
  actual_presentation_time_ = FrameRenderer::kTimeDropped;
  ++frame_presented_count_;

  // Do scheduler-related cleanup.
  if (received_all_callbacks()) {
    Finalize();
  }
}

void FrameTimings::OnFrameSkipped() {
  FX_CHECK(swapchain_records_.empty());

  // Indicates that frame was skipped.
  rendering_finished_time_ = zx::time(async_now(async_get_default_dispatcher()));
  actual_presentation_time_ = zx::time(async_now(async_get_default_dispatcher()));

  frame_was_skipped_ = true;

  // Do scheduler-related cleanup.
  if (received_all_callbacks()) {
    Finalize();
  }
}

void FrameTimings::OnFrameCpuRendered(zx::time time) {
  rendering_cpu_finished_time_ = std::max(rendering_cpu_finished_time_, time);
}

FrameRenderer::Timestamps FrameTimings::GetTimestamps() const {
  // Copy the current time values to a Timestamps struct. Some callers may call
  // this before all times are finalized - it is the caller's responsibility to
  // check if this is |finalized()| if it wants timestamps that are guaranteed
  // not to change. Additionally, some callers will maintain this struct beyond
  // the lifetime of the FrameTimings object (ie for collecting FrameStats), and
  // so the values are copied to allow the FrameTiming object to be destroyed.
  FrameRenderer::Timestamps timestamps = {
      .render_done_time = std::max(rendering_finished_time_, rendering_cpu_finished_time_),
      .actual_presentation_time = actual_presentation_time_,
  };
  return timestamps;
}

void FrameTimings::ValidateRenderTime() {
  FX_DCHECK(rendering_finished_time_ != kTimeUninitialized);
  FX_DCHECK(actual_presentation_time_ != kTimeUninitialized);
  // NOTE: Because there is a delay between when rendering is actually
  // completed and when EventTimestamper generates the timestamp, it's
  // possible that the rendering timestamp is later than the present
  // timestamp. Since we know that's actually impossible, adjust the render
  // timestamp to make it a bit more accurate.
  if (rendering_finished_time_ > actual_presentation_time_) {
    // Reset redering_finished_time_ and adjust rendering times so that they are
    // all less than or equal to the corresponding present time.
    rendering_finished_time_ = kTimeUninitialized;
    for (auto& record : swapchain_records_) {
      FX_DCHECK(record.frame_rendered_time != kTimeUninitialized);
      FX_DCHECK(record.frame_presented_time != kTimeUninitialized);
      if (record.frame_rendered_time > record.frame_presented_time) {
        record.frame_rendered_time = record.frame_presented_time;
      }

      if (record.frame_rendered_time > rendering_finished_time_) {
        rendering_finished_time_ = record.frame_rendered_time;
      }
    }
  }
}

void FrameTimings::Finalize() {
  FX_DCHECK(!finalized());
  finalized_ = true;

  ValidateRenderTime();

  if (timings_presented_callback_) {
    timings_presented_callback_(*this);
  }
}

}  // namespace gfx
}  // namespace scenic_impl
