// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/default_frame_scheduler.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/async/time.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/syscalls.h>

#include <functional>

#include "src/ui/scenic/lib/scheduling/frame_timings.h"

namespace {

template <class T>
static void RemoveSessionIdFromMap(scheduling::SessionId session_id,
                                   std::map<scheduling::SchedulingIdPair, T>* map) {
  auto start = map->lower_bound({session_id, 0});
  auto end = map->lower_bound({session_id + 1, 0});
  map->erase(start, end);
}

}  // namespace

namespace scheduling {

DefaultFrameScheduler::DefaultFrameScheduler(std::shared_ptr<const VsyncTiming> vsync_timing,
                                             std::unique_ptr<FramePredictor> predictor,
                                             inspect::Node inspect_node,
                                             std::shared_ptr<cobalt::CobaltLogger> cobalt_logger)
    : dispatcher_(async_get_default_dispatcher()),
      vsync_timing_(vsync_timing),
      frame_predictor_(std::move(predictor)),
      inspect_node_(std::move(inspect_node)),
      stats_(inspect_node_.CreateChild("Frame Stats"), std::move(cobalt_logger)),
      weak_factory_(this) {
  FX_DCHECK(vsync_timing_);
  FX_DCHECK(frame_predictor_);

  outstanding_frames_.reserve(kMaxOutstandingFrames);

  inspect_frame_number_ = inspect_node_.CreateUint("most_recent_frame_number", frame_number_);
  inspect_last_successful_update_start_time_ =
      inspect_node_.CreateUint("last_successful_update_start_time", 0);
  inspect_last_successful_render_start_time_ =
      inspect_node_.CreateUint("last_successful_render_start_time", 0);
}

DefaultFrameScheduler::~DefaultFrameScheduler() {}

void DefaultFrameScheduler::SetFrameRenderer(std::weak_ptr<FrameRenderer> frame_renderer) {
  FX_DCHECK(frame_renderer_.expired() && !frame_renderer.expired());
  frame_renderer_ = frame_renderer;
}

void DefaultFrameScheduler::AddSessionUpdater(std::weak_ptr<SessionUpdater> session_updater) {
  new_session_updaters_.push_back(std::move(session_updater));
}

void DefaultFrameScheduler::OnFrameRendered(const FrameTimings& timings) {
  TRACE_INSTANT("gfx", "DefaultFrameScheduler::OnFrameRendered", TRACE_SCOPE_PROCESS, "Timestamp",
                timings.GetTimestamps().render_done_time.get(), "frame_number",
                timings.frame_number());

  release_fence_signaller_.SignalFencesUpToAndIncluding(timings.frame_number());

  auto current_timestamps = timings.GetTimestamps();

  if (current_timestamps.render_done_time == FrameTimings::kTimeDropped) {
    return;
  }

  zx::duration duration =
      current_timestamps.render_done_time - current_timestamps.render_start_time;
  FX_DCHECK(duration.get() > 0);

  frame_predictor_->ReportRenderDuration(zx::duration(duration));
}

void DefaultFrameScheduler::SetRenderContinuously(bool render_continuously) {
  render_continuously_ = render_continuously;
  if (render_continuously_) {
    RequestFrame(zx::time(0));
  }
}

PresentId DefaultFrameScheduler::RegisterPresent(
    SessionId session_id, std::variant<OnPresentedCallback, Present2Info> present_information,
    std::vector<zx::event> release_fences, PresentId present_id) {
  present_id = present_id == 0 ? scheduling::GetNextPresentId() : present_id;

  SchedulingIdPair id_pair{session_id, present_id};
  presents_[id_pair] = std::nullopt;  // Initialize an empty entry in |presents_|.

  if (auto present1_callback = std::get_if<OnPresentedCallback>(&present_information)) {
    present1_callbacks_.emplace(id_pair, std::move(*present1_callback));
  } else {
    auto present2_info = std::get_if<Present2Info>(&present_information);
    FX_DCHECK(present2_info);
    present2_infos_.emplace(id_pair, std::move(*present2_info));
  }

  FX_DCHECK(release_fences_.find(id_pair) == release_fences_.end());
  release_fences_.emplace(id_pair, std::move(release_fences));

  return present_id;
}

std::pair<zx::time, zx::time> DefaultFrameScheduler::ComputePresentationAndWakeupTimesForTargetTime(
    const zx::time requested_presentation_time) const {
  const zx::time last_vsync_time = vsync_timing_->last_vsync_time();
  const zx::duration vsync_interval = vsync_timing_->vsync_interval();
  FX_DCHECK(vsync_interval.get() >= 0);
  FX_DCHECK(last_vsync_time.get() >= 0);
  const zx::time now = zx::time(async_now(dispatcher_));

  PredictedTimes times =
      frame_predictor_->GetPrediction({.now = now,
                                       .requested_presentation_time = requested_presentation_time,
                                       .last_vsync_time = last_vsync_time,
                                       .vsync_interval = vsync_interval});

  return std::make_pair(times.presentation_time, times.latch_point_time);
}

void DefaultFrameScheduler::RequestFrame(zx::time requested_presentation_time) {
  FX_DCHECK(HaveUpdatableSessions() || render_continuously_ || render_pending_);

  // Output requested presentation time in milliseconds.
  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 3) {
    FX_VLOGS(1) << "RequestFrame";
  }

  auto next_times = ComputePresentationAndWakeupTimesForTargetTime(requested_presentation_time);
  auto new_target_presentation_time = next_times.first;
  auto new_wakeup_time = next_times.second;

  TRACE_DURATION("gfx", "DefaultFrameScheduler::RequestFrame", "requested presentation time",
                 requested_presentation_time.get() / 1'000'000, "target_presentation_time",
                 new_target_presentation_time.get() / 1'000'000);

  uint64_t trace_id = SESSION_TRACE_ID(request_to_render_count_, new_wakeup_time.get());

  render_wakeup_map_.insert({new_wakeup_time, trace_id});
  ++request_to_render_count_;

  TRACE_FLOW_BEGIN("gfx", "request_to_render", trace_id);

  // If there is no render waiting we should schedule a frame. Likewise, if newly predicted wake up
  // time is earlier than the current one then we need to reschedule the next wake-up.
  if (!frame_render_task_.is_pending() || new_wakeup_time < wakeup_time_) {
    frame_render_task_.Cancel();

    wakeup_time_ = new_wakeup_time;
    next_target_presentation_time_ = new_target_presentation_time;
    frame_render_task_.PostForTime(dispatcher_, zx::time(wakeup_time_));
  }
}

void DefaultFrameScheduler::HandleNextFrameRequest() {
  if (!pending_present_requests_.empty()) {
    auto min_it =
        std::min_element(pending_present_requests_.begin(), pending_present_requests_.end(),
                         [](const auto& left, const auto& right) {
                           const auto leftPresentationTime = left.second;
                           const auto rightPresentationTime = right.second;
                           return leftPresentationTime < rightPresentationTime;
                         });

    RequestFrame(zx::time(min_it->second));
  }
}

void DefaultFrameScheduler::MaybeRenderFrame(async_dispatcher_t*, async::TaskBase*, zx_status_t) {
  FX_DCHECK(!frame_renderer_.expired());

  const uint64_t frame_number = frame_number_;

  {
    // Trace event to track the delta between the targeted wakeup_time_ and the actual wakeup time.
    // It is used to detect delays (i.e. if this thread is blocked on the cpu). The intended
    // wakeup_time_ is used to track the canonical "start" of this frame at various points during
    // the frame's execution.
    const zx::duration wakeup_delta = zx::time(async_now(dispatcher_)) - wakeup_time_;
    TRACE_COUNTER("gfx", "Wakeup Time Delta", /* counter_id */ 0, "delta", wakeup_delta.get());
  }

  const auto target_presentation_time = next_target_presentation_time_;
  TRACE_DURATION("gfx", "FrameScheduler::MaybeRenderFrame", "target_presentation_time",
                 target_presentation_time.get() / 1'000'000);

  // Logging the first few frames to find common startup bugs.
  if (frame_number < 3) {
    FX_VLOGS(1) << "MaybeRenderFrame target_presentation_time=" << target_presentation_time.get()
                << " wakeup_time=" << wakeup_time_.get() << " frame_number=" << frame_number;
  }

  // Apply all updates
  const zx::time update_start_time = zx::time(async_now(dispatcher_));

  // The second value, |wakeup_time_|, here is important for ensuring our flows stay connected.
  // If you change it please ensure the "request_to_render" flow stays connected.
  bool needs_render = ApplyUpdates(target_presentation_time, wakeup_time_, frame_number);

  if (needs_render) {
    inspect_last_successful_update_start_time_.Set(update_start_time.get());
  }

  // TODO(fxbug.dev/24669) Revisit how we do this.
  const zx::time update_end_time = zx::time(async_now(dispatcher_));
  frame_predictor_->ReportUpdateDuration(zx::duration(update_end_time - update_start_time));

  if (!needs_render && !render_pending_ && !render_continuously_) {
    // Nothing to render. Continue with next request in the queue.
    HandleNextFrameRequest();
    return;
  }

  // TODO(fxbug.dev/24531) Remove the render_pending_ check, and pipeline frames within a VSYNC
  // interval.
  if (currently_rendering_) {
    render_pending_ = true;
    return;
  }

  FX_DCHECK(outstanding_frames_.size() < kMaxOutstandingFrames);

  // Logging the first few frames to find common startup bugs.
  if (frame_number < 3) {
    FX_LOGS(INFO) << "Calling RenderFrame target_presentation_time="
                  << target_presentation_time.get() << " frame_number=" << frame_number;
  }

  TRACE_INSTANT("gfx", "Render start", TRACE_SCOPE_PROCESS, "Expected presentation time",
                target_presentation_time.get(), "frame_number", frame_number);
  const zx::time frame_render_start_time = zx::time(async_now(dispatcher_));

  // Create a FrameTimings instance for this frame to track the render and presentation times.
  auto timings_rendered_callback = [weak =
                                        weak_factory_.GetWeakPtr()](const FrameTimings& timings) {
    if (weak) {
      weak->OnFrameRendered(timings);
    } else {
      FX_LOGS(ERROR) << "Error, cannot record render time: FrameScheduler does not exist";
    }
  };

  ++frame_render_trace_id_;
  TRACE_FLOW_BEGIN("gfx", "render_to_presented", frame_render_trace_id_);
  auto timings_presented_callback = [weak = weak_factory_.GetWeakPtr(),
                                     trace_id =
                                         frame_render_trace_id_](const FrameTimings& timings) {
    TRACE_FLOW_END("gfx", "render_to_presented", trace_id);
    if (weak) {
      weak->OnFramePresented(timings);
    } else {
      FX_LOGS(ERROR) << "Error, cannot record presentation time: FrameScheduler does not exist";
    }
  };
  auto frame_timings = std::make_unique<FrameTimings>(
      frame_number, target_presentation_time, wakeup_time_, frame_render_start_time,
      std::move(timings_rendered_callback), std::move(timings_presented_callback));
  // TODO(fxbug.dev/24669) Revisit how we do this.
  frame_timings->OnFrameUpdated(update_end_time);

  inspect_frame_number_.Set(frame_number);

  // Render the frame.
  RenderFrameResult render_frame_result = kRenderFailed;
  if (auto renderer = frame_renderer_.lock()) {
    render_frame_result =
        renderer->RenderFrame(frame_timings->GetWeakPtr(), target_presentation_time);
  }
  currently_rendering_ = render_frame_result == kRenderSuccess;

  // See fxbug.dev/24688 for details of measuring render time.
  const zx::time frame_render_end_cpu_time = zx::time(async_now(dispatcher_));
  frame_timings->OnFrameCpuRendered(frame_render_end_cpu_time);

  switch (render_frame_result) {
    case kRenderSuccess:
      currently_rendering_ = true;
      outstanding_frames_.push_back(std::move(frame_timings));
      render_pending_ = false;

      inspect_last_successful_render_start_time_.Set(target_presentation_time.get());
      break;
    case kRenderFailed:
      // TODO(fxbug.dev/24538): Handle failed rendering somehow.
      FX_LOGS(WARNING) << "RenderFrame failed. "
                       << "There may not be any calls to OnFrameRendered or OnFramePresented, and "
                          "no callbacks may be invoked.";
      break;
    case kNoContentToRender:
      // Immediately invoke presentation callbacks.
      FX_DCHECK(!frame_timings->finalized());
      outstanding_frames_.push_back(std::move(frame_timings));
      outstanding_frames_.back()->OnFrameSkipped();
      break;
  }

  ++frame_number_;

  // Schedule next frame if any unhandled presents are left.
  HandleNextFrameRequest();
}

void DefaultFrameScheduler::ScheduleUpdateForSession(zx::time requested_presentation_time,
                                                     SchedulingIdPair id_pair) {
  TRACE_DURATION("gfx", "DefaultFrameScheduler::ScheduleUpdateForSession",
                 "requested_presentation_time", requested_presentation_time.get() / 1'000'000);

  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 3) {
    FX_VLOGS(1) << "ScheduleUpdateForSession session_id: " << id_pair.session_id
                << " requested_presentation_time: " << requested_presentation_time.get();
  }

  pending_present_requests_.emplace(id_pair, requested_presentation_time);
  RequestFrame(requested_presentation_time);
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
  FX_DCHECK(request.last_vsync_time.get() >= 0);
  FX_DCHECK(request.vsync_interval.get() >= 0);

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

void DefaultFrameScheduler::OnFramePresented(const FrameTimings& timings) {
  const uint64_t frame_number = timings.frame_number();

  if (frame_number_ < 3) {
    FX_LOGS(INFO) << "DefaultFrameScheduler::OnFramePresented"
                  << " frame_number=" << frame_number;
  }

  FX_DCHECK(!outstanding_frames_.empty());

  // TODO(fxbug.dev/23637): how should we handle this case?  It is theoretically possible, but if it
  // happens then it means that the EventTimestamper is receiving signals out-of-order and is
  // therefore generating bogus data.
  FX_DCHECK(outstanding_frames_[0].get() == &timings) << "out-of-order.";

  FX_DCHECK(timings.finalized());
  FX_DCHECK(vsync_timing_->vsync_interval().get() >= 0);
  const FrameTimings::Timestamps timestamps = timings.GetTimestamps();
  stats_.RecordFrame(timestamps, vsync_timing_->vsync_interval());

  if (timings.FrameWasDropped()) {
    TRACE_INSTANT("gfx", "FrameDropped", TRACE_SCOPE_PROCESS, "frame_number", frame_number);
  } else if (timings.FrameWasSkipped()) {
    TRACE_INSTANT("gfx", "FrameDropped", TRACE_SCOPE_PROCESS, "frame_number", frame_number);

    auto presentation_info = fuchsia::images::PresentationInfo();
    presentation_info.presentation_time = timestamps.actual_presentation_time.get();
    presentation_info.presentation_interval = vsync_timing_->vsync_interval().get();

    SignalPresentCallbacksUpTo(frame_number, presentation_info);
  } else {
    if (TRACE_CATEGORY_ENABLED("gfx")) {
      // Log trace data..
      zx::duration target_vs_actual =
          timestamps.actual_presentation_time - timestamps.target_presentation_time;

      zx::time now = zx::time(async_now(dispatcher_));
      zx::duration elapsed_since_presentation = now - timestamps.actual_presentation_time;
      FX_DCHECK(elapsed_since_presentation.get() >= 0);

      TRACE_INSTANT("gfx", "FramePresented", TRACE_SCOPE_PROCESS, "frame_number", frame_number,
                    "presentation time", timestamps.actual_presentation_time.get(),
                    "target time missed by", target_vs_actual.get(),
                    "elapsed time since presentation", elapsed_since_presentation.get());
    }

    auto presentation_info = fuchsia::images::PresentationInfo();
    presentation_info.presentation_time = timestamps.actual_presentation_time.get();
    presentation_info.presentation_interval = vsync_timing_->vsync_interval().get();

    SignalPresentCallbacksUpTo(frame_number, presentation_info);
  }

  // Pop the front Frame off the queue.
  for (size_t i = 1; i < outstanding_frames_.size(); ++i) {
    outstanding_frames_[i - 1] = std::move(outstanding_frames_[i]);
  }
  outstanding_frames_.resize(outstanding_frames_.size() - 1);

  currently_rendering_ = false;
  if (render_continuously_ || render_pending_) {
    RequestFrame(zx::time(0));
  }
}

void DefaultFrameScheduler::RemoveSession(SessionId session_id) {
  present2_callback_map_.erase(session_id);
  RemoveSessionIdFromMap(session_id, &presents_);
  RemoveSessionIdFromMap(session_id, &pending_present_requests_);
  RemoveSessionIdFromMap(session_id, &present1_callbacks_);
  RemoveSessionIdFromMap(session_id, &present2_infos_);
  RemoveSessionIdFromMap(session_id, &release_fences_);
}

std::unordered_map<SessionId, PresentId> DefaultFrameScheduler::CollectUpdatesForThisFrame(
    zx::time target_presentation_time) {
  std::unordered_map<SessionId, PresentId> updates;

  SessionId current_session = 0;
  bool hit_limit = false;
  auto it = pending_present_requests_.begin();
  while (it != pending_present_requests_.end()) {
    auto& [id_pair, requested_presentation_time] = *it;
    if (current_session != id_pair.session_id) {
      current_session = id_pair.session_id;
      hit_limit = false;
    }

    if (!hit_limit && requested_presentation_time <= target_presentation_time) {
      // Return only the last relevant present id for each session.
      updates[current_session] = id_pair.present_id;
      it = pending_present_requests_.erase(it);
    } else {
      hit_limit = true;
      ++it;
    }
  }

  return updates;
}

void DefaultFrameScheduler::PrepareUpdates(const std::unordered_map<SessionId, PresentId>& updates,
                                           zx::time latched_time, uint64_t frame_number) {
  latched_updates_.push({.frame_number = frame_number, .updated_sessions = updates});
  for (const auto& [session_id, present_id] : updates) {
    SetLatchedTimeForPresentsUpTo({session_id, present_id}, latched_time);
    SetLatchedTimeForPresent2Infos({session_id, present_id}, latched_time);
    MoveReleaseFencesToSignaller({session_id, present_id}, frame_number);
  }
}

void DefaultFrameScheduler::RefreshSessionUpdaters() {
  // Add pending SessionUpdaters.
  std::move(new_session_updaters_.begin(), new_session_updaters_.end(),
            std::back_inserter(session_updaters_));
  new_session_updaters_.clear();

  // Clear any stale SessionUpdaters.
  session_updaters_.erase(
      std::remove_if(session_updaters_.begin(), session_updaters_.end(),
                     [](std::weak_ptr<SessionUpdater> updater) { return updater.expired(); }),
      session_updaters_.end());
}

SessionUpdater::UpdateResults DefaultFrameScheduler::ApplyUpdatesToEachUpdater(
    const std::unordered_map<SessionId, PresentId>& sessions_to_update, uint64_t frame_number) {
  // Apply updates to each SessionUpdater.
  SessionUpdater::UpdateResults update_results;
  std::for_each(
      session_updaters_.begin(), session_updaters_.end(),
      [&sessions_to_update, &update_results, frame_number](std::weak_ptr<SessionUpdater> updater) {
        // We still need to check for dead updaters since more could die inside UpdateSessions.
        auto updater_locked = updater.lock();
        if (!updater_locked) {
          return;
        }
        auto session_results = updater_locked->UpdateSessions(sessions_to_update, frame_number);
        // Aggregate results from each updater.
        // Note: Currently, only one SessionUpdater handles each SessionId. If this
        // changes, then a SessionId corresponding to a failed update should not be passed
        // to subsequent SessionUpdaters.
        update_results.sessions_with_failed_updates.insert(
            session_results.sessions_with_failed_updates.begin(),
            session_results.sessions_with_failed_updates.end());
        session_results.sessions_with_failed_updates.clear();
      });

  return update_results;
}

void DefaultFrameScheduler::SetLatchedTimeForPresentsUpTo(SchedulingIdPair id_pair,
                                                          zx::time latched_time) {
  const auto begin_it = presents_.lower_bound({id_pair.session_id, 0});
  const auto end_it = presents_.upper_bound(id_pair);
  std::for_each(begin_it, end_it,
                [latched_time](std::pair<const SchedulingIdPair, std::optional<zx::time>>& pair) {
                  // Update latched time for Present2Infos that haven't already been latched on
                  // previous frames.
                  if (pair.second == std::nullopt)
                    pair.second = latched_time;
                });
}

void DefaultFrameScheduler::SetLatchedTimeForPresent2Infos(SchedulingIdPair id_pair,
                                                           zx::time latched_time) {
  const auto begin_it = present2_infos_.lower_bound({id_pair.session_id, 0});
  const auto end_it = present2_infos_.upper_bound(id_pair);
  std::for_each(begin_it, end_it,
                [latched_time](std::pair<const SchedulingIdPair, Present2Info>& present2_info) {
                  // Update latched time for Present2Infos that haven't already been latched on
                  // previous frames.
                  if (!present2_info.second.HasLatchedTime())
                    present2_info.second.SetLatchedTime(latched_time);
                });
}

void DefaultFrameScheduler::MoveReleaseFencesToSignaller(SchedulingIdPair id_pair,
                                                         uint64_t frame_number) {
  const auto begin_it = release_fences_.lower_bound({id_pair.session_id, 0});
  const auto end_it = release_fences_.lower_bound(id_pair);
  FX_DCHECK(std::distance(begin_it, end_it) >= 0);
  std::for_each(
      begin_it, end_it,
      [this,
       frame_number](std::pair<const SchedulingIdPair, std::vector<zx::event>>& release_fences) {
        release_fence_signaller_.AddFences(std::move(release_fences.second), frame_number);
      });
  release_fences_.erase(begin_it, end_it);
}

bool DefaultFrameScheduler::ApplyUpdates(zx::time target_presentation_time, zx::time latched_time,
                                         uint64_t frame_number) {
  FX_DCHECK(latched_time <= target_presentation_time);

  // Logging the first few frames to find common startup bugs.
  if (frame_number < 3) {
    FX_VLOGS(1) << "ApplyScheduledSessionUpdates target_presentation_time="
                << target_presentation_time.get() << " frame_number=" << frame_number;
  }

  // NOTE: this name is used by scenic_processing_helpers.go
  TRACE_DURATION("gfx", "ApplyScheduledSessionUpdates", "target_presentation_time",
                 target_presentation_time.get() / 1'000'000, "frame_number", frame_number);

  auto it = render_wakeup_map_.begin();
  while (it != render_wakeup_map_.end() && it->first <= latched_time) {
    TRACE_FLOW_END("gfx", "request_to_render", it->second);
    ++it;
  }
  render_wakeup_map_.erase(render_wakeup_map_.begin(), it);

  TRACE_FLOW_BEGIN("gfx", "scenic_frame", frame_number);

  RefreshSessionUpdaters();
  const auto update_map = CollectUpdatesForThisFrame(target_presentation_time);
  const bool have_updates = !update_map.empty();
  if (have_updates) {
    PrepareUpdates(update_map, latched_time, frame_number);
    const auto update_results = ApplyUpdatesToEachUpdater(update_map, frame_number);
    RemoveFailedSessions(update_results.sessions_with_failed_updates);
  }

  // If anything was updated, we need to render.
  return have_updates;
}

void DefaultFrameScheduler::RemoveFailedSessions(
    const std::unordered_set<SessionId>& sessions_with_failed_updates) {
  for (auto failed_session_id : sessions_with_failed_updates) {
    RemoveSession(failed_session_id);
  }
}

// Handle any Present1 and |fuchsia::images::ImagePipe::PresentImage| callbacks.
void DefaultFrameScheduler::SignalPresent1CallbacksUpTo(
    SchedulingIdPair id_pair, fuchsia::images::PresentationInfo presentation_info) {
  auto begin_it = present1_callbacks_.lower_bound({id_pair.session_id, 0});
  auto end_it = present1_callbacks_.upper_bound(id_pair);
  FX_DCHECK(std::distance(begin_it, end_it) >= 0);
  if (begin_it != end_it) {
    std::for_each(
        begin_it, end_it,
        [presentation_info](std::pair<const SchedulingIdPair, OnPresentedCallback>& pair) {
          // TODO(fxbug.dev/24540): Make this unique per session via id().
          TRACE_FLOW_BEGIN("gfx", "present_callback", presentation_info.presentation_time);
          auto& callback = pair.second;
          callback(presentation_info);
        });
    present1_callbacks_.erase(begin_it, end_it);
  }
}

void DefaultFrameScheduler::SignalPresent2CallbackForInfosUpTo(SchedulingIdPair id_pair,
                                                               zx::time presented_time) {
  // Coalesces all Present2 updates and calls the callback once.
  auto begin_it = present2_infos_.lower_bound({id_pair.session_id, 0});
  auto end_it = present2_infos_.upper_bound(id_pair);
  FX_DCHECK(std::distance(begin_it, end_it) >= 0);
  if (begin_it != end_it) {
    std::vector<Present2Info> present2_infos_for_session;
    std::for_each(
        begin_it, end_it,
        [&present2_infos_for_session](std::pair<const SchedulingIdPair, Present2Info>& pair) {
          present2_infos_for_session.emplace_back(std::move(pair.second));
        });
    present2_infos_.erase(begin_it, end_it);

    // TODO(fxbug.dev/24540): Make this unique per session via id().
    TRACE_FLOW_BEGIN("gfx", "present_callback", presented_time.get());
    fuchsia::scenic::scheduling::FramePresentedInfo frame_presented_info =
        Present2Info::CoalescePresent2Infos(std::move(present2_infos_for_session), presented_time);
    FX_DCHECK(present2_callback_map_.find(id_pair.session_id) != present2_callback_map_.end());
    // Invoke the Session's OnFramePresented event.
    present2_callback_map_[id_pair.session_id](std::move(frame_presented_info));
  }
}

void DefaultFrameScheduler::SignalPresentCallbacksUpTo(
    uint64_t frame_number, fuchsia::images::PresentationInfo presentation_info) {
  // Get last present_id up to |frame_number| for each session.
  std::unordered_map<SessionId, PresentId> last_updates;
  std::unordered_map<SessionId, std::map<PresentId, zx::time>> latched_times;
  while (!latched_updates_.empty() && latched_updates_.front().frame_number <= frame_number) {
    for (const auto& [session_id, present_id] : latched_updates_.front().updated_sessions) {
      latched_times[session_id] = ExtractLatchTimestampsUpTo({session_id, present_id});

      last_updates[session_id] = present_id;
    }
    latched_updates_.pop();
  }

  for (const auto& [session_id, present_id] : last_updates) {
    SignalPresent1CallbacksUpTo({session_id, present_id}, presentation_info);
    SignalPresent2CallbackForInfosUpTo({session_id, present_id},
                                       zx::time(presentation_info.presentation_time));
  }

  const PresentTimestamps present_timestamps{
      .presented_time = zx::time(presentation_info.presentation_time),
      .vsync_interval = zx::duration(presentation_info.presentation_interval),
  };
  for (auto updater : session_updaters_) {
    if (auto locked = updater.lock()) {
      locked->OnFramePresented(latched_times, present_timestamps);
    }
  }
}

std::map<PresentId, zx::time> DefaultFrameScheduler::ExtractLatchTimestampsUpTo(
    SchedulingIdPair id_pair) {
  std::map<PresentId, zx::time> timestamps;

  auto begin_it = presents_.lower_bound({id_pair.session_id, 0});
  auto end_it = presents_.upper_bound(id_pair);
  FX_DCHECK(std::distance(begin_it, end_it) >= 0);
  std::for_each(begin_it, end_it,
                [&timestamps](std::pair<const SchedulingIdPair, std::optional<zx::time>>& pair) {
                  FX_DCHECK(pair.second);
                  timestamps[pair.first.present_id] = pair.second.value();
                });
  presents_.erase(begin_it, end_it);

  return timestamps;
}

void DefaultFrameScheduler::SetOnFramePresentedCallbackForSession(
    SessionId session_id, OnFramePresentedCallback frame_presented_callback) {
  FX_DCHECK(present2_callback_map_.find(session_id) == present2_callback_map_.end());
  present2_callback_map_[session_id] = std::move(frame_presented_callback);
}

}  // namespace scheduling
