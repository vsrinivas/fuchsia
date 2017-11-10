// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/engine/frame_scheduler.h"

#include <trace/event.h>
#include <zircon/syscalls.h>

#include "garnet/bin/ui/scene_manager/displays/display.h"
#include "garnet/bin/ui/scene_manager/engine/frame_timings.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"

namespace scene_manager {

// Hard-coded estimate of how long it takes the SceneManager to render a frame.
// TODO: more sophisticated prediction.
constexpr uint64_t kPredictedFrameRenderTime = 8'000'000;  // 8ms

FrameScheduler::FrameScheduler(Display* display)
    : task_runner_(fsl::MessageLoop::GetCurrent()->task_runner().get()),
      display_(display),
      weak_factory_(this) {
  outstanding_frames_.reserve(kMaxOutstandingFrames);
}

FrameScheduler::~FrameScheduler() {}

void FrameScheduler::RequestFrame(uint64_t presentation_time) {
  requested_presentation_times_.push(presentation_time);
  MaybeScheduleFrame();
}

uint64_t FrameScheduler::ComputeTargetPresentationTime(uint64_t now) const {
  if (requested_presentation_times_.empty()) {
    // No presentation was requested.
    return last_presentation_time_;
  }

  // Compute the time that the content would ideally appear on screen: the next
  // Vsync at or after the requested time.
  const uint64_t last_vsync = display_->GetLastVsyncTime();
  const uint64_t vsync_interval = display_->GetVsyncInterval();
  const uint64_t requested_time = requested_presentation_times_.top();
  uint64_t target_time = 0;  // computed below.
  if (last_vsync >= requested_time) {
    // The time has already passed, so target the next vsync.
    target_time = last_vsync + vsync_interval;
  } else {
    // Compute the number of intervals from the last vsync until the requested
    // presentation time, rounded up.  Use this to compute the target frame
    // time.
    const uint64_t num_intervals_to_next_time =
        (requested_time - last_vsync + vsync_interval - 1) / vsync_interval;
    target_time = last_vsync + num_intervals_to_next_time * vsync_interval;
  }

  // Determine how much time we have until the target Vsync.  If this is less
  // than the amount of time that we predict that we will need to render the
  // frame, then target the next Vsync.
  if (now > target_time - kPredictedFrameRenderTime) {
    target_time += vsync_interval;
    FXL_DCHECK(now <= target_time + kPredictedFrameRenderTime);
  }

  // There may be a frame already scheduled for the same or earlier time; if so,
  // we don't need to schedule one ourselves.  In other words, we need to
  // schedule a frame if either:
  // - there is no other frame already scheduled, or
  // - there is a frame scheduled, but for a later time
  if (next_presentation_time_ > last_presentation_time_) {
    if (target_time >= next_presentation_time_) {
      // There is already a frame scheduled for before our target time, so
      // return immediately without scheduling a frame.
      return last_presentation_time_;
    }
  } else {
    // There was no frame scheduled.
    FXL_DCHECK(next_presentation_time_ == last_presentation_time_);
  }

  FXL_DCHECK(target_time > last_presentation_time_);
  return target_time;
}

void FrameScheduler::MaybeScheduleFrame() {
  uint64_t target_time =
      ComputeTargetPresentationTime(zx_time_get(ZX_CLOCK_MONOTONIC));
  if (target_time <= last_presentation_time_) {
    FXL_DCHECK(target_time == last_presentation_time_);
    return;
  }

  // Set the next presentation time to our target, and post a task early enough
  // that we can render and present the resulting image on time.
  next_presentation_time_ = target_time;
  auto time_to_start_rendering =
      fxl::TimePoint::FromEpochDelta(fxl::TimeDelta::FromNanoseconds(
          next_presentation_time_ - kPredictedFrameRenderTime));
  task_runner_->PostTaskForTime(
      [weak = weak_factory_.GetWeakPtr()] {
        if (weak)
          weak->MaybeRenderFrame();
      },
      time_to_start_rendering);
}

void FrameScheduler::MaybeRenderFrame() {
  if (last_presentation_time_ >= next_presentation_time_) {
    FXL_DCHECK(last_presentation_time_ == next_presentation_time_);

    // An earlier frame than us was scheduled, and rendered first.  Therefore,
    // don't render immediately; instead, check if another frame should be
    // scheduled.
    MaybeScheduleFrame();
    return;
  }

  if (TooMuchBackPressure()) {
    // No need to request another frame; MaybeScheduleFrame() will be called
    // when the back-pressure is relieved.
    return;
  }

  // We are about to render a frame for the next scheduled presentation time, so
  // keep only the presentation requests for later times.
  while (!requested_presentation_times_.empty() &&
         next_presentation_time_ >= requested_presentation_times_.top()) {
    requested_presentation_times_.pop();
  }

  // Go render the frame.
  if (delegate_) {
    FXL_DCHECK(outstanding_frames_.size() < kMaxOutstandingFrames);
    auto frame_timings = fxl::MakeRefCounted<FrameTimings>(
        this, ++frame_number_, next_presentation_time_);
    delegate_->RenderFrame(frame_timings, next_presentation_time_,
                           display_->GetVsyncInterval());
    // TODO(MZ-260): enable this.
    // outstanding_frames_.push_back(frame_timings);
  }

  // The frame is in flight, and will be presented.  Check if another frame
  // needs to be scheduled.
  last_presentation_time_ = next_presentation_time_;
  MaybeScheduleFrame();
}

void FrameScheduler::ReceiveFrameTimings(FrameTimings* timings) {
  FXL_DCHECK(!outstanding_frames_.empty());
  // TODO: how should we handle this case?  It is theoretically possible, but if
  // if it happens then it means that the EventTimestamper is receiving signals
  // out-of-order and is therefore generating bogus data.
  FXL_DCHECK(outstanding_frames_[0].get() == timings) << "out-of-order.";

// TODO(MZ-260): enable this.
#if 0
  zx_time_t presentation_time = ????;  // obtain from FrameTimings
  display_->set_last_vsync(timings->actual_presentation_time());
#endif

  // Log trace data.
  // TODO: just pass the whole Frame to a listener.
  int64_t error_usecs =
      static_cast<int64_t>(timings->actual_presentation_time() -
                           timings->target_presentation_time()) /
      1000;
  TRACE_INSTANT("gfx", "FramePresented", TRACE_SCOPE_PROCESS, "frame_number",
                timings->frame_number(), "time",
                timings->actual_presentation_time(), "error", error_usecs);

  // Pop the front Frame off the queue.
  for (size_t i = 1; i < outstanding_frames_.size(); ++i) {
    outstanding_frames_[i - 1] = std::move(outstanding_frames_[i]);
  }
  outstanding_frames_.resize(outstanding_frames_.size() - 1);

  // If a frame was not scheduled due to back-pressure, try again.
  if (back_pressure_applied_) {
    back_pressure_applied_ = false;
    MaybeScheduleFrame();
  }
}

bool FrameScheduler::TooMuchBackPressure() {
// TODO(MZ-260): enable this.
#if 0
  if (outstanding_frames_.size() >= kMaxOutstandingFrames) {
    back_pressure_applied_ = true;
    return true;
  }
#endif
  return false;
}

}  // namespace scene_manager
