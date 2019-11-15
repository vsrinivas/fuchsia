// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/default_frame_scheduler.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/async/time.h>
#include <zircon/syscalls.h>

#include <trace/event.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/gfx/engine/frame_timings.h"
#include "src/ui/scenic/lib/gfx/util/collection_utils.h"
#include "src/ui/scenic/lib/gfx/util/time.h"

namespace scenic_impl {
namespace gfx {

DefaultFrameScheduler::DefaultFrameScheduler(std::shared_ptr<VsyncTiming> vsync_timing,
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
  const zx::time last_vsync_time = vsync_timing_->GetLastVsyncTime();
  const zx::duration vsync_interval = vsync_timing_->GetVsyncInterval();
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

  auto presentation_time = next_presentation_time_;
  TRACE_DURATION("gfx", "FrameScheduler::MaybeRenderFrame", "presentation_time",
                 presentation_time.get());

  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 3) {
    FXL_VLOG(1) << "MaybeRenderFrame presentation_time=" << presentation_time
                << " wakeup_time=" << wakeup_time_ << " frame_number=" << frame_number_;
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
    FXL_LOG(INFO) << "Calling RenderFrame presentation_time=" << presentation_time
                  << " frame_number=" << frame_number_;
  }

  TRACE_INSTANT("gfx", "Render start", TRACE_SCOPE_PROCESS, "Expected presentation time",
                presentation_time.get(), "frame_number", frame_number_);
  const zx::time frame_render_start_time = zx::time(async_now(dispatcher_));

  // Ratchet the Present callbacks to signal that all outstanding Present() calls until this point
  // are applied to the next Scenic frame.
  update_manager_.RatchetPresentCallbacks(presentation_time, frame_number_);

  auto frame_timings = fxl::MakeRefCounted<FrameTimings>(this, frame_number_, presentation_time,
                                                         wakeup_time_, frame_render_start_time);
  // TODO(SCN-1482) Revisit how we do this.
  frame_timings->OnFrameUpdated(update_end_time);

  inspect_frame_number_.Set(frame_number_);

  // Render the frame.
  auto render_frame_result = frame_renderer_->RenderFrame(frame_timings, presentation_time);
  currently_rendering_ = render_frame_result == kRenderSuccess;

  // See SCN-1505 for details of measuring render time.
  const zx::time frame_render_end_cpu_time = zx::time(async_now(dispatcher_));
  frame_timings->OnFrameCpuRendered(frame_render_end_cpu_time);

  switch (render_frame_result) {
    case kRenderSuccess:
      currently_rendering_ = true;
      outstanding_frames_.push_back(frame_timings);
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
                                                     scenic_impl::SessionId session_id) {
  update_manager_.ScheduleUpdate(presentation_time, session_id);

  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 3) {
    FXL_VLOG(1) << "ScheduleUpdateForSession session_id: " << session_id
                << " presentation_time: " << presentation_time;
  }

  RequestFrame();
}

std::vector<fuchsia::scenic::scheduling::PresentationInfo>
DefaultFrameScheduler::GetFuturePresentationInfos(zx::duration requested_prediction_span) {
  std::vector<fuchsia::scenic::scheduling::PresentationInfo> infos;

  PredictionRequest request;
  request.now = zx::time(async_now(dispatcher_));
  request.last_vsync_time = vsync_timing_->GetLastVsyncTime();

  // We assume this value is constant, at least for the near future.
  request.vsync_interval = vsync_timing_->GetVsyncInterval();

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
  return infos;
}

void DefaultFrameScheduler::SetOnFramePresentedCallbackForSession(
    scenic_impl::SessionId session, OnFramePresentedCallback callback) {
  update_manager_.SetOnFramePresentedCallbackForSession(session, std::move(callback));
}

DefaultFrameScheduler::UpdateManager::ApplyUpdatesResult DefaultFrameScheduler::ApplyUpdates(
    zx::time target_presentation_time, zx::time latched_time) {
  FXL_DCHECK(latched_time <= target_presentation_time);
  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 3) {
    FXL_VLOG(1) << "ApplyScheduledSessionUpdates presentation_time=" << target_presentation_time
                << " frame_number=" << frame_number_;
  }

  return update_manager_.ApplyUpdates(target_presentation_time, latched_time,
                                      vsync_timing_->GetVsyncInterval(), frame_number_);
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
  stats_.RecordFrame(timestamps, vsync_timing_->GetVsyncInterval());

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
    presentation_info.presentation_interval = vsync_timing_->GetVsyncInterval().get();

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

  SessionUpdater::UpdateResults update_results;
  ApplyToCompactedVector(
      &session_updaters_, [this, &sessions_to_update, &update_results, target_presentation_time,
                           latched_time, frame_number](SessionUpdater* updater) {
        auto session_results = updater->UpdateSessions(sessions_to_update, target_presentation_time,
                                                       latched_time, frame_number);

        // Aggregate results from each updater.
        update_results.needs_render = update_results.needs_render || session_results.needs_render;
        update_results.sessions_to_reschedule.insert(session_results.sessions_to_reschedule.begin(),
                                                     session_results.sessions_to_reschedule.end());

        MoveAllItemsFromQueueToQueue(&session_results.present1_callbacks,
                                     &present1_callbacks_this_frame_);
        MoveAllItemsFromQueueToQueue(&session_results.present2_infos, &present2_infos_this_frame_);
      });

  // Push updates that (e.g.) had unreached fences back onto the queue to be retried next frame.
  for (auto session_id : update_results.sessions_to_reschedule) {
    updatable_sessions_.push(
        {.session_id = session_id,
         .requested_presentation_time = target_presentation_time + vsync_interval});
  }

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
  MoveAllItemsFromQueueToQueue(&present1_callbacks_this_frame_, &pending_present1_callbacks_);

  // Populate the Present2 multimap.
  while (!present2_infos_this_frame_.empty()) {
    auto element = std::move(present2_infos_this_frame_.front());

    pending_present2_infos_.insert(std::make_pair(element.session_id(), std::move(element)));
    present2_infos_this_frame_.pop();
  }

  ApplyToCompactedVector(&session_updaters_,
                         [presentation_time, frame_number](SessionUpdater* updater) {
                           updater->PrepareFrame(presentation_time, frame_number);
                         });
}

void DefaultFrameScheduler::UpdateManager::SignalPresentCallbacks(
    fuchsia::images::PresentationInfo presentation_info) {
  // Handle Present1 and |fuchsia::images::ImagePipe::PresentImage| callbacks.
  while (!pending_present1_callbacks_.empty()) {
    // TODO(SCN-1346): Make this unique per session via id().
    TRACE_FLOW_BEGIN("gfx", "present_callback", presentation_info.presentation_time);
    pending_present1_callbacks_.front()(presentation_info);
    pending_present1_callbacks_.pop();
  }

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

    FXL_DCHECK(present2_callback_map_.find(current_session) != present2_callback_map_.end());
    // TODO(SCN-1346): Make this unique per session via id().
    TRACE_FLOW_BEGIN("gfx", "present_callback", presentation_info.presentation_time);

    fuchsia::scenic::scheduling::FramePresentedInfo frame_presented_info =
        scenic_impl::Present2Info::CoalescePresent2Infos(
            std::move(present2_infos), zx::time(presentation_info.presentation_time));

    // Invoke the Session's OnFramePresented event.
    present2_callback_map_[current_session](std::move(frame_presented_info));
  }
  FXL_DCHECK(pending_present2_infos_.size() == 0u);
}

void DefaultFrameScheduler::UpdateManager::SetOnFramePresentedCallbackForSession(
    scenic_impl::SessionId session, OnFramePresentedCallback callback) {
  FXL_DCHECK(present2_callback_map_.find(session) == present2_callback_map_.end());
  present2_callback_map_[session] = std::move(callback);
}

}  // namespace gfx
}  // namespace scenic_impl
