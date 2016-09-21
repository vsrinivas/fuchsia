// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/compositor/src/backend/vsync_scheduler.h"

#include <algorithm>

#include "apps/compositor/glue/base/trace_event.h"
#include "lib/ftl/logging.h"

namespace compositor {

constexpr int64_t VsyncScheduler::kMinVsyncInterval;
constexpr int64_t VsyncScheduler::kMaxVsyncInterval;

VsyncScheduler::VsyncScheduler(const ftl::RefPtr<ftl::TaskRunner>& task_runner,
                               const SchedulerCallbacks& callbacks)
    : VsyncScheduler(task_runner, callbacks, &MojoGetTimeTicksNow) {}

VsyncScheduler::VsyncScheduler(const ftl::RefPtr<ftl::TaskRunner>& task_runner,
                               const SchedulerCallbacks& callbacks,
                               const Clock& clock)
    : state_(std::make_shared<State>(task_runner, callbacks, clock)) {}

VsyncScheduler::~VsyncScheduler() {}

void VsyncScheduler::ScheduleFrame(SchedulingMode scheduling_mode) {
  state_->ScheduleFrame(scheduling_mode);
}

VsyncScheduler::State::State(const ftl::RefPtr<ftl::TaskRunner>& task_runner,
                             const SchedulerCallbacks& callbacks,
                             const Clock& clock)
    : task_runner_(task_runner), callbacks_(callbacks), clock_(clock) {}

VsyncScheduler::State::~State() {}

bool VsyncScheduler::State::Start(int64_t vsync_timebase,
                                  int64_t vsync_interval,
                                  int64_t update_phase,
                                  int64_t snapshot_phase,
                                  int64_t presentation_phase) {
  // Be slightly paranoid.  Timing glitches are hard to find and the
  // vsync parameters will typically come from other services.
  // Ensure vsync timing is anchored on actual observations from the past.
  MojoTimeTicks now = GetTimeTicksNow();
  if (vsync_timebase > now) {
    FTL_LOG(WARNING) << "Vsync timebase is in the future: vsync_timebase="
                     << vsync_timebase << ", now=" << now;
    return false;
  }
  if (vsync_interval < kMinVsyncInterval ||
      vsync_interval > kMaxVsyncInterval) {
    FTL_LOG(WARNING) << "Vsync interval is invalid: vsync_interval="
                     << vsync_interval << ", min=" << kMinVsyncInterval
                     << ", max=" << kMaxVsyncInterval;
    return false;
  }
  if (snapshot_phase < update_phase ||
      snapshot_phase > update_phase + vsync_interval ||
      presentation_phase < snapshot_phase) {
    // Updating and snapshotting must happen within the same frame interval
    // to avoid having multiple updates in progress simultanteously (which
    // doesn't make much sense if we're already compute bound).
    FTL_LOG(WARNING) << "Vsync scheduling phases are invalid: update_phase="
                     << update_phase << ", snapshot_phase=" << snapshot_phase
                     << ", presentation_phase=" << presentation_phase;
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);

    // Suppress spurious updates.
    if (running_ && vsync_timebase_ == vsync_timebase &&
        vsync_interval_ == vsync_interval && update_phase_ == update_phase &&
        snapshot_phase_ == snapshot_phase &&
        presentation_phase_ == presentation_phase)
      return true;

    // Get running with these new parameters.
    // Note that |last_delivered_update_time_| and
    // |last_delivered_presentation_time_| are preserved.
    running_ = true;
    generation_++;  // cancels pending undelivered callbacks
    vsync_timebase_ = vsync_timebase;
    vsync_interval_ = vsync_interval;
    update_phase_ = update_phase;
    snapshot_phase_ = snapshot_phase;
    presentation_phase_ = presentation_phase;
    need_update_ = true;
    pending_dispatch_ = false;
    ScheduleLocked(now);
    return true;
  }
}

void VsyncScheduler::State::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = false;
}

void VsyncScheduler::State::ScheduleFrame(SchedulingMode scheduling_mode) {
  MojoTimeTicks now = GetTimeTicksNow();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
      if (scheduling_mode == SchedulingMode::kUpdateAndSnapshot)
        need_update_ = true;
      ScheduleLocked(now);
    }
  }
}

void VsyncScheduler::State::ScheduleLocked(MojoTimeTicks now) {
  TRACE_EVENT2("gfx", "VsyncScheduler::ScheduleLocked", "pending_dispatch",
               pending_dispatch_, "need_update", need_update_);

  FTL_DCHECK(running_);
  FTL_DCHECK(now >= vsync_timebase_);

  if (pending_dispatch_)
    return;

  // Determine the time of the earliest achievable frame snapshot in
  // the near future.
  int64_t snapshot_timebase = vsync_timebase_ + snapshot_phase_;
  uint64_t snapshot_offset = (now - snapshot_timebase) % vsync_interval_;
  int64_t snapshot_time = now - snapshot_offset + vsync_interval_;
  FTL_DCHECK(snapshot_time >= now);

  // Determine when the update that produced this snapshot must have begun.
  // This time may be in the past.
  int64_t update_time = snapshot_time - snapshot_phase_ + update_phase_;
  FTL_DCHECK(update_time <= snapshot_time);
  int64_t presentation_time =
      snapshot_time - snapshot_phase_ + presentation_phase_;

  // When changing vsync parameters, it's possible for the next update or
  // presentation time to regress.  Prevent applications from observing that
  // by skipping frames if needed to preserve monotonicity.
  if (update_time <= last_delivered_update_time_ ||
      presentation_time <= last_delivered_presentation_time_) {
    int64_t delay =
        std::max(last_delivered_update_time_ - update_time,
                 last_delivered_presentation_time_ - presentation_time);
    int64_t frames = delay / vsync_interval_ + 1;
    int64_t adjustment = frames * vsync_interval_;
    update_time += adjustment;
    snapshot_time += adjustment;
  }

  // Schedule dispatching at that time.
  if (update_time >= now) {
    PostDispatchLocked(now, update_time, Action::kUpdate, update_time);
  } else {
    PostDispatchLocked(now, snapshot_time, Action::kEarlySnapshot, update_time);
  }

  pending_dispatch_ = true;
}

void VsyncScheduler::State::PostDispatchLocked(int64_t now,
                                               int64_t delivery_time,
                                               Action action,
                                               int64_t update_time) {
  TRACE_EVENT2("gfx", "VsyncScheduler::PostDispatchLocked", "delivery_time",
               delivery_time, "update_time", update_time);

  const std::weak_ptr<State> state_weak = shared_from_this();
  const int64_t generation = generation_;

  task_runner_->PostDelayedTask(
      [state_weak, generation, action, update_time] {
        std::shared_ptr<State> state = state_weak.lock();
        if (state)
          state->Dispatch(generation, action, update_time);
      },
      ftl::TimeDelta::FromMicroseconds(
          std::max(delivery_time - now, static_cast<int64_t>(0))));
}

void VsyncScheduler::State::Dispatch(int32_t generation,
                                     Action action,
                                     int64_t update_time) {
  TRACE_EVENT2("gfx", "VsyncScheduler::Dispatch", "action",
               static_cast<int>(action), "update_time", update_time);

  MojoTimeTicks now = GetTimeTicksNow();
  FTL_DCHECK(update_time <= now);

  // Time may have passed since the callback was originally scheduled and
  // it's possible that we completely missed the deadline we were aiming for.
  // Reevaluate the schedule and jump ahead if necessary.
  mojo::gfx::composition::FrameInfo frame_info;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || generation_ != generation)
      return;

    FTL_DCHECK(pending_dispatch_);

    // Check whether we missed any deadlines.
    bool missed_deadline = false;
    if (action == Action::kUpdate) {
      int64_t update_deadline = update_time - update_phase_ + snapshot_phase_;
      if (now > update_deadline) {
        FTL_DLOG(WARNING) << "Compositor missed update deadline by "
                          << (now - update_deadline) << " us";
        missed_deadline = true;
      }
    } else {
      int64_t snapshot_deadline = update_time + vsync_interval_;
      if (now > snapshot_deadline) {
        FTL_DLOG(WARNING) << "Compositor missed snapshot deadline by "
                          << (now - snapshot_deadline) << " us";
        missed_deadline = true;
      }
    }
    if (missed_deadline) {
      uint64_t offset = (now - update_time) % vsync_interval_;
      update_time = now - offset;
      FTL_DCHECK(update_time > now - vsync_interval_ && update_time <= now);
    }

    // Schedule the corresponding snapshot for the update.
    if (action == Action::kUpdate) {
      int64_t snapshot_time = update_time - update_phase_ + snapshot_phase_;
      PostDispatchLocked(now, snapshot_time, Action::kLateSnapshot,
                         update_time);
      need_update_ = false;
    } else if (need_update_) {
      int64_t next_update_time = update_time + vsync_interval_;
      PostDispatchLocked(now, next_update_time, Action::kUpdate,
                         next_update_time);

      // If we missed the deadline on an early snapshot, then just skip it
      // and wait for the following update instead.
      if (action == Action::kEarlySnapshot && missed_deadline) {
        TRACE_EVENT_INSTANT0(
            "gfx", "VsyncScheduler::StateDispatch Skipped early snapshot",
            TRACE_EVENT_SCOPE_THREAD);
        return;
      }
    } else {
      pending_dispatch_ = false;
    }

    SetFrameInfoLocked(&frame_info, update_time);
    last_delivered_update_time_ = update_time;
    last_delivered_presentation_time_ = frame_info.presentation_time;
  }

  if (action == Action::kUpdate) {
    callbacks_.update_callback(frame_info);
  } else {
    callbacks_.snapshot_callback(frame_info);
  }
}

void VsyncScheduler::State::SetFrameInfoLocked(
    mojo::gfx::composition::FrameInfo* frame_info,
    int64_t update_time) {
  FTL_DCHECK(frame_info);
  frame_info->frame_time = update_time;
  frame_info->frame_interval = vsync_interval_;
  frame_info->frame_deadline = update_time - update_phase_ + snapshot_phase_;
  frame_info->presentation_time =
      update_time - update_phase_ + presentation_phase_;
}

}  // namespace compositor
