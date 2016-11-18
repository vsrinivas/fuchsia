// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/scheduler.h"

#include "apps/tracing/lib/trace/event.h"
#include "lib/mtl/tasks/message_loop.h"

namespace compositor {
namespace {

// The amount of time to allow for snapshot operations to complete before
// a frame is submitted to the output.
// TODO(jeffbrown): Measure this, don't hardcode it.
constexpr ftl::TimeDelta kSnapshotLatency =
    ftl::TimeDelta::FromMicroseconds(2000);

// When to yell about missing deadlines.
constexpr ftl::TimeDelta kDeadlineTolerance =
    ftl::TimeDelta::FromMicroseconds(4000);

}  // namespace

Scheduler::Scheduler(Output* output)
    : output_(output),
      task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()),
      weak_ptr_factory_(this) {
  FTL_DCHECK(output_);
}

Scheduler::~Scheduler() {}

void Scheduler::SetCallbacks(FrameCallback update_callback,
                             FrameCallback snapshot_callback) {
  update_callback_ = update_callback;
  snapshot_callback_ = snapshot_callback;
}

void Scheduler::ScheduleFrame(SchedulingMode scheduling_mode) {
  TRACE_EVENT1("gfx", "ScheduleFrame", "scheduling_mode", scheduling_mode);

  if (scheduling_mode == SchedulingMode::kUpdateThenSnapshot)
    update_pending_ = true;

  if (frame_scheduled_)
    return;
  frame_scheduled_ = true;

  // Note: OnFrameScheduled may be called immediately.
  output_->ScheduleFrame([weak = weak_ptr_factory_.GetWeakPtr()](
      const Output::FrameTiming& timing) {
    if (weak)
      weak->OnFrameScheduled(timing);
  });
}

void Scheduler::OnFrameScheduled(const Output::FrameTiming& timing) {
  TRACE_EVENT0("gfx", "OnFrameScheduled");
  FTL_DCHECK(timing.presentation_interval > ftl::TimeDelta::Zero());
  FTL_DCHECK(timing.presentation_latency >= ftl::TimeDelta::Zero());
  FTL_DCHECK(frame_scheduled_);

  ftl::TimePoint now = ftl::TimePoint::Now();

  // Figure out how much time we want to allow for the next update and snapshot.
  ftl::TimeDelta next_presentation_interval = timing.presentation_interval;
  ftl::TimeDelta next_update_budget = timing.presentation_latency;
  ftl::TimeDelta next_snapshot_budget = kSnapshotLatency;

  // Determine the time of the next achievable snapshot.
  ftl::TimeDelta snapshot_to_presentation =
      next_presentation_interval + next_snapshot_budget;
  ftl::TimePoint next_snapshot_time =
      timing.presentation_time - snapshot_to_presentation;
  if (next_snapshot_time < now) {
    ftl::TimeDelta phase =
        (now - next_snapshot_time) % next_presentation_interval;
    next_snapshot_time = now + next_presentation_interval - phase;
    FTL_DCHECK(next_snapshot_time >= now);
  }
  ftl::TimePoint next_update_time = next_snapshot_time - next_update_budget;
  ftl::TimePoint next_presentation_time =
      next_snapshot_time + snapshot_to_presentation;

  // When adapting to changing frame rates, increasing pipeline latency, or
  // skipped frames, it's possible for the time references to appear to
  // regress.  Skip ahead if that happens.  (This should be rare!)
  if (next_presentation_time <= last_presentation_time_ ||
      next_snapshot_time <= last_snapshot_time_ ||
      next_update_time <= last_update_time_) {
    ftl::TimeDelta overlap =
        std::max({last_presentation_time_ - next_presentation_time,
                  last_snapshot_time_ - next_snapshot_time,
                  last_update_time_ - next_update_time});
    int64_t skipped_frames = (overlap / next_presentation_interval) + 1;
    FTL_VLOG(1) << "Skipping " << skipped_frames
                << " to prevent time running backwards";
    ftl::TimeDelta adjustment = next_presentation_interval * skipped_frames;
    next_presentation_time = next_presentation_time + adjustment;
    next_snapshot_time = next_snapshot_time + adjustment;
    next_update_time = next_update_time + adjustment;
  }
  FTL_DCHECK(next_presentation_time > last_presentation_time_);
  FTL_DCHECK(next_snapshot_time >= now);
  FTL_DCHECK(next_snapshot_time > last_snapshot_time_);
  FTL_DCHECK(next_snapshot_time <= next_presentation_time);
  FTL_DCHECK(next_update_time > last_update_time_);
  FTL_DCHECK(next_update_time <= next_snapshot_time);
  last_presentation_time_ = next_presentation_time;
  last_snapshot_time_ = next_snapshot_time;
  last_update_time_ = next_update_time;

  // Build frame info for the next frame.
  FrameInfo next_frame_info;
  next_frame_info.presentation_time = next_presentation_time;
  next_frame_info.presentation_interval = next_presentation_interval;
  next_frame_info.publish_deadline = next_snapshot_time;
  next_frame_info.base_time = next_update_time;

  // If we have time for an update, then always schedule it.
  // Otherwise schedule a snapshot and do the update later.
  if (next_update_time >= now || (update_pending_ && prevent_stall_)) {
    if (next_update_time < now) {
      // If snapshots take way too long to complete, we can end up in a
      // situation where the next update gets deferred indefinitely.
      // Prevent this from happening.
      FTL_VLOG(1) << "Scheduled late update to prevent stalls";
    } else {
      prevent_stall_ = false;
    }
    PostUpdate(next_frame_info);
  } else {
    prevent_stall_ = true;
    PostSnapshot(next_frame_info);
  }
}

void Scheduler::PostUpdate(const FrameInfo& frame_info) {
  task_runner_->PostTaskForTime(
      [ weak = weak_ptr_factory_.GetWeakPtr(), frame_info ] {
        if (weak)
          weak->OnUpdate(frame_info);
      },
      frame_info.base_time);
}

void Scheduler::PostSnapshot(const FrameInfo& frame_info) {
  task_runner_->PostTaskForTime(
      [ weak = weak_ptr_factory_.GetWeakPtr(), frame_info ] {
        if (weak)
          weak->OnSnapshot(frame_info);
      },
      frame_info.publish_deadline);
}

void Scheduler::OnUpdate(const FrameInfo& frame_info) {
  TRACE_EVENT0("gfx", "OnUpdate");
  FTL_DCHECK(frame_scheduled_);

  // Yell if we completely missed the deadline.
  ftl::TimePoint now = ftl::TimePoint::Now();
  ftl::TimePoint deadline = frame_info.base_time + kDeadlineTolerance;
  if (deadline < now) {
    FTL_VLOG(1) << "Compositor missed frame update deadline by "
                << (now - deadline).ToMillisecondsF() << " ms";
  }

  // Schedule the upcoming snapshot.
  PostSnapshot(frame_info);

  // Do the update.
  // This may cause reentrance into |ScheduleFrame|.
  if (update_pending_) {
    update_pending_ = false;
    update_callback_(frame_info);
  }
}

void Scheduler::OnSnapshot(const FrameInfo& frame_info) {
  TRACE_EVENT0("gfx", "OnSnapshot");
  FTL_DCHECK(frame_scheduled_);

  // Yell if we completely missed the deadline.
  ftl::TimePoint now = ftl::TimePoint::Now();
  ftl::TimePoint deadline = frame_info.publish_deadline + kDeadlineTolerance;
  if (deadline < now) {
    FTL_VLOG(1) << "Compositor missed frame snapshot deadline by "
                << (now - deadline).ToMillisecondsF() << " ms";
  }

  // Now that we are finishing this frame, schedule the next one if needed.
  frame_scheduled_ = false;
  if (update_pending_)
    ScheduleFrame(SchedulingMode::kUpdateThenSnapshot);

  // Do the snapshot.
  // This may cause reentrance into |ScheduleFrame|.
  snapshot_callback_(frame_info);
}

}  // namespace compositor
