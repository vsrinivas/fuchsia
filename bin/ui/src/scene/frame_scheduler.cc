// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/frame_scheduler.h"

#include <magenta/syscalls.h>

#include "apps/mozart/src/scene/display.h"
#include "apps/mozart/src/scene/renderer/renderer.h"
#include "apps/mozart/src/scene/resources/nodes/scene.h"
#include "ftl/logging.h"

namespace mozart {
namespace scene {

// Hard-coded estimate of how long it takes the SceneManager to render a frame.
// TODO: more sophisticated prediction.
constexpr uint64_t kPredictedFrameRenderTime = 4'000'000;  // 4ms

FrameScheduler::FrameScheduler(Display* display)
    : task_runner_(mtl::MessageLoop::GetCurrent()->task_runner().get()),
      display_(display) {}

FrameScheduler::~FrameScheduler() {}

void FrameScheduler::AddRenderer(Renderer* renderer) {
  FTL_DCHECK(renderer);
  FTL_DCHECK(renderer->frame_scheduler() == this);

  FTL_DCHECK(renderers_.empty()) << "Only one Renderer is currently supported.";

  bool success = renderers_.insert(renderer).second;
  FTL_DCHECK(success) << "Renderer was already added to FrameScheduler.";
}

void FrameScheduler::RemoveRenderer(Renderer* renderer) {
  FTL_DCHECK(renderer);
  FTL_DCHECK(renderer->frame_scheduler() == this);
  size_t count = renderers_.erase(renderer);
  FTL_DCHECK(count == 1) << "Renderer was not removed from FrameScheduler.";
}

void FrameScheduler::AddListener(FrameSchedulerListener* listener) {
  FTL_DCHECK(listener);
  bool success = listeners_.insert(listener).second;
  FTL_DCHECK(success) << "Listener was already added to FrameScheduler.";
}

void FrameScheduler::RemoveListener(FrameSchedulerListener* listener) {
  FTL_DCHECK(listener);
  size_t count = listeners_.erase(listener);
  FTL_DCHECK(count == 1) << "Listener was not removed from FrameScheduler.";
}

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
    FTL_DCHECK(now <= target_time + kPredictedFrameRenderTime);
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
    FTL_DCHECK(next_presentation_time_ == last_presentation_time_);
  }

  FTL_DCHECK(target_time > last_presentation_time_);
  return target_time;
}

void FrameScheduler::MaybeScheduleFrame() {
  uint64_t target_time =
      ComputeTargetPresentationTime(mx_time_get(MX_CLOCK_MONOTONIC));
  if (target_time <= last_presentation_time_) {
    FTL_DCHECK(target_time == last_presentation_time_);
    return;
  }

  // Set the next presentation time to our target, and post a task early enough
  // that we can render and present the resulting image on time.
  next_presentation_time_ = target_time;
  auto time_to_start_rendering =
      ftl::TimePoint::FromEpochDelta(ftl::TimeDelta::FromNanoseconds(
          next_presentation_time_ - kPredictedFrameRenderTime));
  task_runner_->PostTaskForTime([this] { MaybeUpdateSceneAndDrawFrame(); },
                                time_to_start_rendering);
}

void FrameScheduler::MaybeUpdateSceneAndDrawFrame() {
  if (last_presentation_time_ >= next_presentation_time_) {
    FTL_DCHECK(last_presentation_time_ == next_presentation_time_);

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

  // A frame should be drawn now.  Notify listeners to update the global scene.
  UpdateScene();

  if (!renderers_.empty()) {
    DrawFrame();
  } else {
    // The only renderer could have been destroyed, if the session holding it
    // threw an error.
    FTL_LOG(ERROR) << "FrameScheduler::MaybeUpdateSceneAndDrawFrame: No "
                      "renderers available.";
  }
  // The frame is in flight, and will be presented.  Check if another frame
  // needs to be scheduled.
  last_presentation_time_ = next_presentation_time_;
  MaybeScheduleFrame();
}

bool FrameScheduler::TooMuchBackPressure() {
  // TODO: implement back-pressure in case we can't hit our desired frame rate.
  // If this returns true, then MaybeScheduleFrame() MUST be called once the
  // back-pressure is relieved.
  return false;
}

void FrameScheduler::UpdateScene() {
  // We are about to render a frame for the next scheduled presentation time, so
  // keep only the presentation requests for later times.
  while (!requested_presentation_times_.empty() &&
         next_presentation_time_ >= requested_presentation_times_.top()) {
    requested_presentation_times_.pop();
  }

  // Notify listeners in order to update the global scene.
  bool presentation_is_desired = false;
  const uint64_t presentation_interval = display_->GetVsyncInterval();
  for (auto listener : listeners_) {
    presentation_is_desired |= listener->OnPrepareFrame(next_presentation_time_,
                                                        presentation_interval);
  }
  // We shouldn't be rendering a frame if no listener needed to be updated.
  FTL_DCHECK(presentation_is_desired);
}

void FrameScheduler::DrawFrame() {
  // Only a single renderer is currently supported.
  FTL_DCHECK(renderers_.size() == 1);
  auto renderer = *renderers_.begin();
  renderer->DrawFrame();
}

}  // namespace scene
}  // namespace mozart
