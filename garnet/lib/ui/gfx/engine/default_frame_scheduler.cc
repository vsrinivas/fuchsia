// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/default_frame_scheduler.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/async/time.h>
#include <src/lib/fxl/logging.h>
#include <trace/event.h>
#include <zircon/syscalls.h>

#include "garnet/lib/ui/gfx/displays/display.h"
#include "garnet/lib/ui/gfx/engine/frame_timings.h"
#include "garnet/lib/ui/gfx/util/collection_utils.h"

namespace scenic_impl {
namespace gfx {

DefaultFrameScheduler::DefaultFrameScheduler(const Display* display,
                                             std::unique_ptr<FramePredictor> predictor,
                                             inspect::Node inspect_node)
    : dispatcher_(async_get_default_dispatcher()),
      display_(display),
      frame_predictor_(std::move(predictor)),
      inspect_node_(std::move(inspect_node)),
      stats_(inspect_node_.CreateChild("Frame Stats")),
      weak_factory_(this) {
  FXL_DCHECK(display_);
  FXL_DCHECK(frame_predictor_);

  outstanding_frames_.reserve(kMaxOutstandingFrames);

  inspect_frame_number_ = inspect_node_.CreateUIntMetric("most_recent_frame_number", frame_number_);
  inspect_last_successful_update_start_time_ =
      inspect_node_.CreateUIntMetric("inspect_last_successful_update_start_time_", 0);
  inspect_last_successful_render_start_time_ =
      inspect_node_.CreateUIntMetric("inspect_last_successful_render_start_time_", 0);
}

DefaultFrameScheduler::~DefaultFrameScheduler() {}

void DefaultFrameScheduler::SetFrameRenderer(fxl::WeakPtr<FrameRenderer> frame_renderer) {
  FXL_DCHECK(!frame_renderer_ && frame_renderer);
  frame_renderer_ = frame_renderer;
}

void DefaultFrameScheduler::AddSessionUpdater(fxl::WeakPtr<SessionUpdater> session_updater) {
  update_manager_.AddSessionUpdater(std::move(session_updater));
}

void DefaultFrameScheduler::OnFrameRendered(const FrameTimings& timings) {
  TRACE_INSTANT("gfx", "DefaultFrameScheduler::OnFrameRendered", TRACE_SCOPE_PROCESS, "Timestamp",
                timings.GetTimestamps().render_done_time, "frame_number", timings.frame_number());

  auto current_timestamps = timings.GetTimestamps();

  if (current_timestamps.render_done_time == FrameTimings::kTimeDropped) {
    return;
  }

  zx_duration_t duration =
      current_timestamps.render_done_time - current_timestamps.render_start_time;
  FXL_DCHECK(duration > 0);

  frame_predictor_->ReportRenderDuration(zx::duration(duration));
}

void DefaultFrameScheduler::SetRenderContinuously(bool render_continuously) {
  render_continuously_ = render_continuously;
  if (render_continuously_) {
    RequestFrame();
  }
}

std::pair<zx_time_t, zx_time_t>
DefaultFrameScheduler::ComputePresentationAndWakeupTimesForTargetTime(
    const zx_time_t requested_presentation_time) const {
  const zx_time_t last_vsync_time = display_->GetLastVsyncTime();
  const zx_duration_t vsync_interval = display_->GetVsyncInterval();
  const zx_time_t now = async_now(dispatcher_);

  // TODO(SCN-1467): Standardize zx::time/zx_time_t use.
  PredictedTimes times = frame_predictor_->GetPrediction(
      {.now = zx::time(now),
       .requested_presentation_time = zx::time(requested_presentation_time),
       .last_vsync_time = zx::time(last_vsync_time),
       .vsync_interval = zx::duration(vsync_interval)});

  return std::make_pair(times.presentation_time.get(), times.latch_point_time.get());
}

void DefaultFrameScheduler::RequestFrame() {
  FXL_DCHECK(update_manager_.HasUpdatableSessions() || render_continuously_ || render_pending_);

  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 3) {
    FXL_LOG(INFO) << "RequestFrame";
  }

  zx_time_t requested_presentation_time = render_continuously_ || render_pending_
                                              ? 0
                                              : update_manager_.EarliestRequestedPresentationTime();

  auto next_times = ComputePresentationAndWakeupTimesForTargetTime(requested_presentation_time);
  auto new_presentation_time = next_times.first;
  auto new_wakeup_time = next_times.second;

  // If there is no render waiting we should schedule a frame.  Likewise, if newly predicted wake up
  // time is earlier than the current one then we need to reschedule the next wake-up.
  if (!frame_render_task_.is_pending() || new_wakeup_time < wakeup_time_) {
    frame_render_task_.Cancel();

    wakeup_time_ = new_wakeup_time;
    next_presentation_time_ = new_presentation_time;
    frame_render_task_.PostForTime(dispatcher_, zx::time(wakeup_time_));
  }
}

void DefaultFrameScheduler::MaybeRenderFrame(async_dispatcher_t*, async::TaskBase*, zx_status_t) {
  FXL_DCHECK(frame_renderer_);

  auto presentation_time = next_presentation_time_;
  TRACE_DURATION("gfx", "FrameScheduler::MaybeRenderFrame", "presentation_time", presentation_time);

  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 3) {
    FXL_LOG(INFO) << "MaybeRenderFrame presentation_time=" << presentation_time
                  << " wakeup_time=" << wakeup_time_ << " frame_number=" << frame_number_;
  }

  // Apply all updates
  const zx_time_t update_start_time = async_now(dispatcher_);

  const UpdateManager::ApplyUpdatesResult update_result = ApplyUpdates(presentation_time);

  if (update_result.needs_render) {
    inspect_last_successful_update_start_time_.Set(update_start_time);
  }

  // TODO(SCN-1482) Revisit how we do this.
  const zx_time_t update_end_time = async_now(dispatcher_);
  frame_predictor_->ReportUpdateDuration(zx::duration(update_end_time - update_start_time));

  if (!update_result.needs_render && !render_pending_ && !render_continuously_) {
    // If necessary, schedule another frame.
    if (update_result.needs_reschedule) {
      RequestFrame();
    }
    return;
  }

  // TODO(SCN-1337) Remove the render_pending_ check, and pipeline frames within a VSYNC interval.
  if (currently_rendering_) {
    render_pending_ = true;
    return;
  }

  FXL_DCHECK(outstanding_frames_.size() < kMaxOutstandingFrames);

  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 3) {
    FXL_LOG(INFO) << "Calling RenderFrame presentation_time=" << presentation_time
                  << " frame_number=" << frame_number_;
  }
  TRACE_INSTANT("gfx", "Render start", TRACE_SCOPE_PROCESS, "Expected presentation time",
                presentation_time, "frame_number", frame_number_);

  // Ratchet the Present callbacks to signal that all outstanding Present() calls until this point
  // are applied to the next Scenic frame.
  update_manager_.RatchetPresentCallbacks(presentation_time, frame_number_);

  const zx_time_t frame_render_start_time = async_now(dispatcher_);
  auto frame_timings = fxl::MakeRefCounted<FrameTimings>(this, frame_number_, presentation_time,
                                                         wakeup_time_, frame_render_start_time);
  // TODO(SCN-1482) Revisit how we do this.
  frame_timings->OnFrameUpdated(update_end_time);

  inspect_frame_number_.Set(frame_number_);

  // Render the frame.
  currently_rendering_ = frame_renderer_->RenderFrame(frame_timings, presentation_time);
  if (currently_rendering_) {
    outstanding_frames_.push_back(frame_timings);
    render_pending_ = false;

    inspect_last_successful_render_start_time_.Set(presentation_time);
  } else {
    // TODO(SCN-1344): Handle failed rendering somehow.
    FXL_LOG(WARNING) << "RenderFrame failed. "
                     << "There may not be any calls to OnFrameRendered or OnFramePresented, "
                     << "and no callbacks may be invoked.";
  }

  ++frame_number_;

  // If necessary, schedule another frame.
  if (update_result.needs_reschedule) {
    RequestFrame();
  }
}

void DefaultFrameScheduler::ScheduleUpdateForSession(zx_time_t presentation_time,
                                                     scenic_impl::SessionId session_id) {
  update_manager_.ScheduleUpdate(presentation_time, session_id);

  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 3) {
    FXL_LOG(INFO) << "ScheduleUpdateForSession session_id: " << session_id
                  << " presentation_time: " << presentation_time;
  }

  RequestFrame();
}

DefaultFrameScheduler::UpdateManager::ApplyUpdatesResult DefaultFrameScheduler::ApplyUpdates(
    zx_time_t presentation_time) {
  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 3) {
    FXL_LOG(INFO) << "ApplyScheduledSessionUpdates presentation_time=" << presentation_time
                  << " frame_number=" << frame_number_;
  }

  return update_manager_.ApplyUpdates(presentation_time, display_->GetVsyncInterval(),
                                      frame_number_);
}

void DefaultFrameScheduler::OnFramePresented(const FrameTimings& timings) {
  if (frame_number_ < 5) {
    FXL_LOG(INFO) << "DefaultFrameScheduler::OnFramePresented"
                  << " frame_number=" << timings.frame_number();
  }

  FXL_DCHECK(!outstanding_frames_.empty());
  // TODO(SCN-400): how should we handle this case?  It is theoretically possible, but if it happens
  // then it means that the EventTimestamper is receiving signals out-of-order and is therefore
  // generating bogus data.
  FXL_DCHECK(outstanding_frames_[0].get() == &timings) << "out-of-order.";

  FXL_DCHECK(timings.finalized());
  const FrameTimings::Timestamps timestamps = timings.GetTimestamps();
  stats_.RecordFrame(timestamps, display_->GetVsyncInterval());

  if (timings.FrameWasDropped()) {
    TRACE_INSTANT("gfx", "FrameDropped", TRACE_SCOPE_PROCESS, "frame_number",
                  timings.frame_number());
  } else {
    if (TRACE_CATEGORY_ENABLED("gfx")) {
      // Log trace data..
      zx_duration_t target_vs_actual =
          timestamps.actual_presentation_time - timestamps.target_presentation_time;

      zx_time_t now = async_now(dispatcher_);
      zx_duration_t elapsed_since_presentation = now - timestamps.actual_presentation_time;
      FXL_DCHECK(elapsed_since_presentation >= 0);

      TRACE_INSTANT("gfx", "FramePresented", TRACE_SCOPE_PROCESS, "frame_number",
                    timings.frame_number(), "presentation time",
                    timestamps.actual_presentation_time, "target time missed by", target_vs_actual,
                    "elapsed time since presentation", elapsed_since_presentation);
    }

    auto presentation_info = fuchsia::images::PresentationInfo();
    presentation_info.presentation_time = timestamps.actual_presentation_time;
    presentation_info.presentation_interval = display_->GetVsyncInterval();

    update_manager_.SignalPresentCallbacks(presentation_info);
  }

  // Pop the front Frame off the queue.
  for (size_t i = 1; i < outstanding_frames_.size(); ++i) {
    outstanding_frames_[i - 1] = std::move(outstanding_frames_[i]);
  }
  outstanding_frames_.resize(outstanding_frames_.size() - 1);

  currently_rendering_ = false;
  if (render_continuously_ || render_pending_) {
    RequestFrame();
  }
}

void DefaultFrameScheduler::UpdateManager::AddSessionUpdater(
    fxl::WeakPtr<SessionUpdater> session_updater) {
  FXL_DCHECK(session_updater);
  session_updaters_.push_back(std::move(session_updater));
}

DefaultFrameScheduler::UpdateManager::ApplyUpdatesResult
DefaultFrameScheduler::UpdateManager::ApplyUpdates(zx_time_t presentation_time,
                                                   zx_time_t vsync_interval,
                                                   uint64_t frame_number) {
  // NOTE: this name is used by scenic_processing_helpers.go
  TRACE_DURATION("gfx", "ApplyScheduledSessionUpdates", "time", presentation_time);

  std::unordered_set<SessionId> sessions_to_update;
  while (!updatable_sessions_.empty() &&
         updatable_sessions_.top().requested_presentation_time <= presentation_time) {
    sessions_to_update.insert(updatable_sessions_.top().session_id);
    updatable_sessions_.pop();
  }

  SessionUpdater::UpdateResults update_results;
  ApplyToCompactedVector(&session_updaters_, [this, &sessions_to_update, &update_results,
                                              presentation_time,
                                              frame_number](SessionUpdater* updater) {
    auto session_results =
        updater->UpdateSessions(sessions_to_update, presentation_time, frame_number);

    // Aggregate results from each updater.
    update_results.needs_render = update_results.needs_render || session_results.needs_render;
    update_results.sessions_to_reschedule.insert(session_results.sessions_to_reschedule.begin(),
                                                 session_results.sessions_to_reschedule.end());

    SessionUpdater::MoveCallbacksFromTo(&session_results.present_callbacks, &callbacks_this_frame_);
  });

  // Push updates that (e.g.) had unreached fences back onto the queue to be retried next frame.
  for (auto session_id : update_results.sessions_to_reschedule) {
    updatable_sessions_.push({.session_id = session_id,
                              .requested_presentation_time = presentation_time + vsync_interval});
  }

  return ApplyUpdatesResult{.needs_render = update_results.needs_render,
                            .needs_reschedule = !updatable_sessions_.empty()};
}

void DefaultFrameScheduler::UpdateManager::ScheduleUpdate(zx_time_t presentation_time,
                                                          SessionId session_id) {
  updatable_sessions_.push(
      {.session_id = session_id, .requested_presentation_time = presentation_time});
}

void DefaultFrameScheduler::UpdateManager::RatchetPresentCallbacks(zx_time_t presentation_time,
                                                                   uint64_t frame_number) {
  SessionUpdater::MoveCallbacksFromTo(&callbacks_this_frame_, &pending_callbacks_);
  ApplyToCompactedVector(&session_updaters_,
                         [presentation_time, frame_number](SessionUpdater* updater) {
                           updater->PrepareFrame(presentation_time, frame_number);
                         });
}

void DefaultFrameScheduler::UpdateManager::SignalPresentCallbacks(
    fuchsia::images::PresentationInfo presentation_info) {
  while (!pending_callbacks_.empty()) {
    // TODO(SCN-1346): Make this unique per session via id().
    TRACE_FLOW_BEGIN("gfx", "present_callback", presentation_info.presentation_time);
    pending_callbacks_.front()(presentation_info);
    pending_callbacks_.pop();
  }
}

}  // namespace gfx
}  // namespace scenic_impl
