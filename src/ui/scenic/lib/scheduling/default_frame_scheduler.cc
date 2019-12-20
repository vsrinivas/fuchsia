// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/default_frame_scheduler.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/async/time.h>
#include <zircon/syscalls.h>

#include <functional>

#include <trace/event.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/scheduling/frame_timings.h"

using scheduling::Present2Info;

namespace scheduling {

DefaultFrameScheduler::DefaultFrameScheduler(std::shared_ptr<const VsyncTiming> vsync_timing,
                                             std::unique_ptr<FramePredictor> predictor,
                                             inspect_deprecated::Node inspect_node,
                                             std::unique_ptr<cobalt::CobaltLogger> cobalt_logger)
    : dispatcher_(async_get_default_dispatcher()),
      vsync_timing_(vsync_timing),
      frame_predictor_(std::move(predictor)),
      inspect_node_(std::move(inspect_node)),
      stats_(inspect_node_.CreateChild("Frame Stats"), std::move(cobalt_logger)),
      weak_factory_(this) {
  FXL_DCHECK(vsync_timing_);
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
                timings.GetTimestamps().render_done_time.get(), "frame_number",
                timings.frame_number());

  auto current_timestamps = timings.GetTimestamps();

  if (current_timestamps.render_done_time == FrameTimings::kTimeDropped) {
    return;
  }

  zx::duration duration =
      current_timestamps.render_done_time - current_timestamps.render_start_time;
  FXL_DCHECK(duration.get() > 0);

  frame_predictor_->ReportRenderDuration(zx::duration(duration));
}

void DefaultFrameScheduler::SetRenderContinuously(bool render_continuously) {
  render_continuously_ = render_continuously;
  if (render_continuously_) {
    RequestFrame();
  }
}

std::pair<zx::time, zx::time> DefaultFrameScheduler::ComputePresentationAndWakeupTimesForTargetTime(
    const zx::time requested_presentation_time) const {
  const zx::time last_vsync_time = vsync_timing_->last_vsync_time();
  const zx::duration vsync_interval = vsync_timing_->vsync_interval();
  const zx::time now = zx::time(async_now(dispatcher_));

  PredictedTimes times =
      frame_predictor_->GetPrediction({.now = now,
                                       .requested_presentation_time = requested_presentation_time,
                                       .last_vsync_time = last_vsync_time,
                                       .vsync_interval = vsync_interval});

  return std::make_pair(times.presentation_time, times.latch_point_time);
}

void DefaultFrameScheduler::RequestFrame() {
  FXL_DCHECK(update_manager_.HasUpdatableSessions() || render_continuously_ || render_pending_);

  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 3) {
    FXL_VLOG(1) << "RequestFrame";
  }

  zx::time requested_presentation_time = render_continuously_ || render_pending_
                                             ? zx::time(0)
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

  {
    // Trace event to track the delta between the targeted wakeup_time_ and the actual wakeup time.
    // It is used to detect delays (i.e. if this thread is blocked on the cpu). The intended
    // wakeup_time_ is used to track the canonical "start" of this frame at various points during
    // the frame's execution.
    const zx::duration wakeup_delta = zx::time(async_now(dispatcher_)) - wakeup_time_;
    TRACE_COUNTER("gfx", "Wakeup Time Delta", /* counter_id */ 0, "delta", wakeup_delta.get());
  }

  auto presentation_time = next_presentation_time_;
  TRACE_DURATION("gfx", "FrameScheduler::MaybeRenderFrame", "presentation_time",
                 presentation_time.get());

  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 3) {
    FXL_VLOG(1) << "MaybeRenderFrame presentation_time=" << presentation_time.get()
                << " wakeup_time=" << wakeup_time_.get() << " frame_number=" << frame_number_;
  }

  // Apply all updates
  const zx::time update_start_time = zx::time(async_now(dispatcher_));

  const UpdateManager::ApplyUpdatesResult update_result =
      ApplyUpdates(presentation_time, wakeup_time_);

  if (update_result.needs_render) {
    inspect_last_successful_update_start_time_.Set(update_start_time.get());
  }

  // TODO(SCN-1482) Revisit how we do this.
  const zx::time update_end_time = zx::time(async_now(dispatcher_));
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
    FXL_LOG(INFO) << "Calling RenderFrame presentation_time=" << presentation_time.get()
                  << " frame_number=" << frame_number_;
  }

  TRACE_INSTANT("gfx", "Render start", TRACE_SCOPE_PROCESS, "Expected presentation time",
                presentation_time.get(), "frame_number", frame_number_);
  const zx::time frame_render_start_time = zx::time(async_now(dispatcher_));

  // Ratchet the Present callbacks to signal that all outstanding Present() calls until this point
  // are applied to the next Scenic frame.
  update_manager_.RatchetPresentCallbacks(presentation_time, frame_number_);

  // Create a FrameTimings instance for this frame to track the render and presentation times.
  auto timings_rendered_callback = [weak =
                                        weak_factory_.GetWeakPtr()](const FrameTimings& timings) {
    if (weak) {
      weak->OnFrameRendered(timings);
    } else {
      FXL_LOG(ERROR) << "Error, cannot record render time: FrameScheduler does not exist";
    }
  };
  auto timings_presented_callback = [weak =
                                         weak_factory_.GetWeakPtr()](const FrameTimings& timings) {
    if (weak) {
      weak->OnFramePresented(timings);
    } else {
      FXL_LOG(ERROR) << "Error, cannot record presentation time: FrameScheduler does not exist";
    }
  };
  auto frame_timings = std::make_unique<FrameTimings>(
      frame_number_, presentation_time, wakeup_time_, frame_render_start_time,
      std::move(timings_rendered_callback), std::move(timings_presented_callback));
  // TODO(SCN-1482) Revisit how we do this.
  frame_timings->OnFrameUpdated(update_end_time);

  inspect_frame_number_.Set(frame_number_);

  // Render the frame.
  auto render_frame_result =
      frame_renderer_->RenderFrame(frame_timings->GetWeakPtr(), presentation_time);
  currently_rendering_ = render_frame_result == kRenderSuccess;

  // See SCN-1505 for details of measuring render time.
  const zx::time frame_render_end_cpu_time = zx::time(async_now(dispatcher_));
  frame_timings->OnFrameCpuRendered(frame_render_end_cpu_time);

  switch (render_frame_result) {
    case kRenderSuccess:
      currently_rendering_ = true;
      outstanding_frames_.push_back(std::move(frame_timings));
      render_pending_ = false;

      inspect_last_successful_render_start_time_.Set(presentation_time.get());
      break;
    case kRenderFailed:
      // TODO(SCN-1344): Handle failed rendering somehow.
      FXL_LOG(WARNING) << "RenderFrame failed. "
                       << "There may not be any calls to OnFrameRendered or OnFramePresented, and "
                          "no callbacks may be invoked.";
      break;
    case kNoContentToRender:
      // Don't do anything.
      break;
  }

  ++frame_number_;

  // If necessary, schedule another frame.
  if (update_result.needs_reschedule) {
    RequestFrame();
  }
}

void DefaultFrameScheduler::ScheduleUpdateForSession(zx::time presentation_time,
                                                     SessionId session_id) {
  update_manager_.ScheduleUpdate(presentation_time, session_id);

  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 3) {
    FXL_VLOG(1) << "ScheduleUpdateForSession session_id: " << session_id
                << " presentation_time: " << presentation_time.get();
  }

  RequestFrame();
}

void DefaultFrameScheduler::GetFuturePresentationInfos(
    zx::duration requested_prediction_span,
    FrameScheduler::GetFuturePresentationInfosCallback presentation_infos_callback) {
  std::vector<fuchsia::scenic::scheduling::PresentationInfo> infos;

  PredictionRequest request;
  request.now = zx::time(async_now(dispatcher_));
  request.last_vsync_time = vsync_timing_->last_vsync_time();

  // We assume this value is constant, at least for the near future.
  request.vsync_interval = vsync_timing_->vsync_interval();

  constexpr static const uint64_t kMaxPredictionCount = 8;
  uint64_t count = 0;

  zx::time prediction_limit = request.now + requested_prediction_span;
  while (request.now <= prediction_limit && count < kMaxPredictionCount) {
    // We ask for a "0 time" in order to give us the next possible presentation time. It also fits
    // the Present() pattern most Scenic clients currently use.
    request.requested_presentation_time = zx::time(0);

    PredictedTimes times = frame_predictor_->GetPrediction(request);
    fuchsia::scenic::scheduling::PresentationInfo info =
        fuchsia::scenic::scheduling::PresentationInfo();
    info.set_latch_point(times.latch_point_time.get());
    info.set_presentation_time(times.presentation_time.get());
    infos.push_back(std::move(info));

    // The new now time is one tick after the returned latch point. This ensures uniqueness in the
    // results we give to the client since we know we cannot schedule a frame for a latch point in
    // the past.
    //
    // We also guarantee loop termination by the same token. Latch points are monotonically
    // increasing, which means so is |request.now| so it will eventually reach prediction_limit.
    request.now = times.latch_point_time + zx::duration(1);

    // last_vsync_time should be the greatest value less than request.now where a vsync
    // occurred. We can calculate this inductively by adding vsync_intervals to last_vsync_time.
    // Therefore what we add to last_vsync_time is the difference between now and
    // last_vsync_time, integer divided by vsync_interval, then multipled by vsync_interval.
    //
    // Because now' is the latch_point, and latch points are monotonically increasing, we guarantee
    // that |difference| and therefore last_vsync_time is also monotonically increasing.
    zx::duration difference = request.now - request.last_vsync_time;
    uint64_t num_intervals = difference / request.vsync_interval;
    request.last_vsync_time += request.vsync_interval * num_intervals;

    ++count;
  }

  ZX_DEBUG_ASSERT(infos.size() >= 1);
  presentation_infos_callback(std::move(infos));
}

void DefaultFrameScheduler::SetOnUpdateFailedCallbackForSession(
    SessionId session, OnSessionUpdateFailedCallback update_failed_callback) {
  update_manager_.SetOnUpdateFailedCallbackForSession(session, std::move(update_failed_callback));
}

void DefaultFrameScheduler::ClearCallbacksForSession(SessionId session_id) {
  update_manager_.ClearCallbacksForSession(session_id);
}

void DefaultFrameScheduler::SetOnFramePresentedCallbackForSession(
    SessionId session, OnFramePresentedCallback frame_presented_callback) {
  update_manager_.SetOnFramePresentedCallbackForSession(session,
                                                        std::move(frame_presented_callback));
}

DefaultFrameScheduler::UpdateManager::ApplyUpdatesResult DefaultFrameScheduler::ApplyUpdates(
    zx::time target_presentation_time, zx::time latched_time) {
  FXL_DCHECK(latched_time <= target_presentation_time);
  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 3) {
    FXL_VLOG(1) << "ApplyScheduledSessionUpdates presentation_time="
                << target_presentation_time.get() << " frame_number=" << frame_number_;
  }

  return update_manager_.ApplyUpdates(target_presentation_time, latched_time,
                                      vsync_timing_->vsync_interval(), frame_number_);
}

void DefaultFrameScheduler::OnFramePresented(const FrameTimings& timings) {
  if (frame_number_ < 3) {
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
  stats_.RecordFrame(timestamps, vsync_timing_->vsync_interval());

  if (timings.FrameWasDropped()) {
    TRACE_INSTANT("gfx", "FrameDropped", TRACE_SCOPE_PROCESS, "frame_number",
                  timings.frame_number());
  } else {
    if (TRACE_CATEGORY_ENABLED("gfx")) {
      // Log trace data..
      zx::duration target_vs_actual =
          timestamps.actual_presentation_time - timestamps.target_presentation_time;

      zx::time now = zx::time(async_now(dispatcher_));
      zx::duration elapsed_since_presentation = now - timestamps.actual_presentation_time;
      FXL_DCHECK(elapsed_since_presentation.get() >= 0);

      TRACE_INSTANT("gfx", "FramePresented", TRACE_SCOPE_PROCESS, "frame_number",
                    timings.frame_number(), "presentation time",
                    timestamps.actual_presentation_time.get(), "target time missed by",
                    target_vs_actual.get(), "elapsed time since presentation",
                    elapsed_since_presentation.get());
    }

    auto presentation_info = fuchsia::images::PresentationInfo();
    presentation_info.presentation_time = timestamps.actual_presentation_time.get();
    presentation_info.presentation_interval = vsync_timing_->vsync_interval().get();

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

void DefaultFrameScheduler::UpdateManager::RemoveSession(SessionId session_id) {
  ClearCallbacksForSession(session_id);
  present1_callbacks_this_frame_.erase(session_id);
  pending_present1_callbacks_.erase(session_id);
  present2_infos_this_frame_.erase(session_id);
  pending_present2_infos_.erase(session_id);

  // Temporary priority queue to hold SessionUpdates that are still valid while all
  // requests associated with session_id are removed.
  // Yes, this is not the most optimal way to remove from the queue. RemoveSession should be called
  // rarely.
  std::priority_queue<SessionUpdate, std::vector<SessionUpdate>, std::greater<SessionUpdate>>
      requests_with_session_id_removed;
  while (!updatable_sessions_.empty()) {
    auto update = updatable_sessions_.top();
    if (update.session_id != session_id) {
      requests_with_session_id_removed.push(update);
    }
    updatable_sessions_.pop();
  }
  updatable_sessions_ = std::move(requests_with_session_id_removed);
}

DefaultFrameScheduler::UpdateManager::ApplyUpdatesResult
DefaultFrameScheduler::UpdateManager::ApplyUpdates(zx::time target_presentation_time,
                                                   zx::time latched_time,
                                                   zx::duration vsync_interval,
                                                   uint64_t frame_number) {
  // NOTE: this name is used by scenic_processing_helpers.go
  TRACE_DURATION("gfx", "ApplyScheduledSessionUpdates", "time", target_presentation_time.get());

  std::unordered_set<SessionId> sessions_to_update;
  while (!updatable_sessions_.empty() &&
         updatable_sessions_.top().requested_presentation_time <= target_presentation_time) {
    sessions_to_update.insert(updatable_sessions_.top().session_id);
    updatable_sessions_.pop();
  }

  // Clear any stale SessionUpdaters.
  session_updaters_.erase(
      std::remove_if(session_updaters_.begin(), session_updaters_.end(), std::logical_not()),
      session_updaters_.end());

  // Apply updates to each SessionUpdater.
  SessionUpdater::UpdateResults update_results;
  std::for_each(
      session_updaters_.begin(), session_updaters_.end(),
      [this, &sessions_to_update, &update_results, target_presentation_time, latched_time,
       frame_number](fxl::WeakPtr<SessionUpdater> updater) {
        auto session_results = updater->UpdateSessions(sessions_to_update, target_presentation_time,
                                                       latched_time, frame_number);

        // Aggregate results from each updater.
        update_results.needs_render = update_results.needs_render || session_results.needs_render;
        update_results.sessions_to_reschedule.insert(session_results.sessions_to_reschedule.begin(),
                                                     session_results.sessions_to_reschedule.end());
        update_results.sessions_with_failed_updates.insert(
            session_results.sessions_with_failed_updates.begin(),
            session_results.sessions_with_failed_updates.end());

        std::move(
            session_results.present1_callbacks.begin(), session_results.present1_callbacks.end(),
            std::inserter(present1_callbacks_this_frame_, present1_callbacks_this_frame_.end()));
        session_results.present1_callbacks.clear();
        std::move(session_results.present2_infos.begin(), session_results.present2_infos.end(),
                  std::inserter(present2_infos_this_frame_, present2_infos_this_frame_.end()));
        session_results.present2_infos.clear();
      });

  // Aggregate all failed session callbacks, and remove failed sessions from all present callback
  // maps.
  std::vector<OnSessionUpdateFailedCallback> failure_callbacks;
  for (auto failed_session_id : update_results.sessions_with_failed_updates) {
    auto it = update_failed_callback_map_.find(failed_session_id);
    FXL_DCHECK(it != update_failed_callback_map_.end());
    failure_callbacks.emplace_back(std::move(it->second));

    // Remove failed sessions from future scheduled updates.
    update_results.sessions_to_reschedule.erase(failed_session_id);
    // Remove the callback from the global map so they are not called after this failure callback is
    // triggered.
    RemoveSession(failed_session_id);
  }

  // Push updates that (e.g.) had unreached fences back onto the queue to be retried next frame.
  for (auto session_id : update_results.sessions_to_reschedule) {
    updatable_sessions_.push(
        {.session_id = session_id,
         .requested_presentation_time = target_presentation_time + vsync_interval});
  }

  // Process all update failed callbacks.
  for (auto& callback : failure_callbacks) {
    callback();
  }
  failure_callbacks.clear();

  return ApplyUpdatesResult{.needs_render = update_results.needs_render,
                            .needs_reschedule = !updatable_sessions_.empty()};
}

void DefaultFrameScheduler::UpdateManager::ScheduleUpdate(zx::time presentation_time,
                                                          SessionId session_id) {
  updatable_sessions_.push(
      {.session_id = session_id, .requested_presentation_time = presentation_time});
}

void DefaultFrameScheduler::UpdateManager::RatchetPresentCallbacks(zx::time presentation_time,
                                                                   uint64_t frame_number) {
  std::move(present1_callbacks_this_frame_.begin(), present1_callbacks_this_frame_.end(),
            std::inserter(pending_present1_callbacks_, pending_present1_callbacks_.end()));
  present1_callbacks_this_frame_.clear();

  std::move(present2_infos_this_frame_.begin(), present2_infos_this_frame_.end(),
            std::inserter(pending_present2_infos_, pending_present2_infos_.end()));
  present2_infos_this_frame_.clear();

  std::for_each(session_updaters_.begin(), session_updaters_.end(),
                [presentation_time, frame_number](fxl::WeakPtr<SessionUpdater> updater) {
                  if (updater) {
                    updater->PrepareFrame(presentation_time, frame_number);
                  }
                });
}

void DefaultFrameScheduler::UpdateManager::SignalPresentCallbacks(
    fuchsia::images::PresentationInfo presentation_info) {
  // Handle Present1 and |fuchsia::images::ImagePipe::PresentImage| callbacks.
  for (auto& [session_id, on_presented_callback] : pending_present1_callbacks_) {
    // TODO(SCN-1346): Make this unique per session via id().
    TRACE_FLOW_BEGIN("gfx", "present_callback", presentation_info.presentation_time);
    on_presented_callback(presentation_info);
  }
  pending_present1_callbacks_.clear();

  // Handle per-Present2() |Present2Info|s.
  // This outer loop iterates through all unique |SessionId|s that have pending Present2 updates.
  SessionId current_session = 0;
  for (auto it = pending_present2_infos_.begin(); it != pending_present2_infos_.end();
       it = pending_present2_infos_.upper_bound(current_session)) {
    current_session = it->first;

    // This inner loop creates a vector of the corresponding |Present2Info|s and coalesces them.
    std::vector<Present2Info> present2_infos = {};
    auto [start_iter, end_iter] = pending_present2_infos_.equal_range(current_session);
    for (auto iter = start_iter; iter != end_iter; ++iter) {
      present2_infos.push_back(std::move(iter->second));
    }
    pending_present2_infos_.erase(start_iter, end_iter);

    if (present2_callback_map_.find(current_session) != present2_callback_map_.end()) {
      // TODO(SCN-1346): Make this unique per session via id().
      TRACE_FLOW_BEGIN("gfx", "present_callback", presentation_info.presentation_time);

      fuchsia::scenic::scheduling::FramePresentedInfo frame_presented_info =
          Present2Info::CoalescePresent2Infos(std::move(present2_infos),
                                              zx::time(presentation_info.presentation_time));

      // Invoke the Session's OnFramePresented event.
      present2_callback_map_[current_session](std::move(frame_presented_info));
    }
  }
  FXL_DCHECK(pending_present2_infos_.size() == 0u);
}

void DefaultFrameScheduler::UpdateManager::SetOnUpdateFailedCallbackForSession(
    SessionId session_id, FrameScheduler::OnSessionUpdateFailedCallback update_failed_callback) {
  FXL_DCHECK(update_failed_callback_map_.find(session_id) == update_failed_callback_map_.end());
  update_failed_callback_map_[session_id] = std::move(update_failed_callback);
}

void DefaultFrameScheduler::UpdateManager::SetOnFramePresentedCallbackForSession(
    SessionId session_id, OnFramePresentedCallback frame_presented_callback) {
  FXL_DCHECK(present2_callback_map_.find(session_id) == present2_callback_map_.end());
  present2_callback_map_[session_id] = std::move(frame_presented_callback);
}

void DefaultFrameScheduler::UpdateManager::ClearCallbacksForSession(SessionId session_id) {
  update_failed_callback_map_.erase(session_id);
  present2_callback_map_.erase(session_id);
}

}  // namespace scheduling
