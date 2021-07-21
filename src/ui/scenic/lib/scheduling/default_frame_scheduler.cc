// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/default_frame_scheduler.h"

#include <lib/async/default.h>
#include <lib/async/time.h>
#include <lib/syslog/cpp/macros.h>

#include <functional>

namespace {

static const uint64_t kNumDebugFrames = 3;

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

  inspect_frame_number_ = inspect_node_.CreateUint("most_recent_frame_number", frame_number_);
  inspect_wakeups_without_render_ = inspect_node_.CreateUint("wakeups_without_rendering", 0);
  inspect_last_successful_update_start_time_ =
      inspect_node_.CreateUint("last_successful_update_start_time", 0);
  inspect_last_successful_render_start_time_ =
      inspect_node_.CreateUint("last_successful_render_start_time", 0);
}

DefaultFrameScheduler::~DefaultFrameScheduler() {}

void DefaultFrameScheduler::Initialize(
    std::weak_ptr<FrameRenderer> frame_renderer,
    std::vector<std::weak_ptr<SessionUpdater>> session_updaters) {
  FX_CHECK(!initialized_);
  initialized_ = true;
  frame_renderer_ = std::move(frame_renderer);
  session_updaters_ = std::move(session_updaters);
}

void DefaultFrameScheduler::SetRenderContinuously(bool render_continuously) {
  render_continuously_ = render_continuously;
  if (render_continuously_) {
    RequestFrame(zx::time(0));
  }
}

PresentId DefaultFrameScheduler::RegisterPresent(SessionId session_id,
                                                 std::vector<zx::event> release_fences,
                                                 PresentId present_id) {
  present_id = present_id == kInvalidPresentId ? scheduling::GetNextPresentId() : present_id;

  SchedulingIdPair id_pair{session_id, present_id};
  presents_[id_pair] = std::nullopt;  // Initialize an empty entry in |presents_|.

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
  FX_DCHECK(HaveUpdatableSessions() || render_continuously_ || !last_frame_is_presented_);

  // Output requested presentation time in milliseconds.
  // Logging the first few frames to find common startup bugs.
  if (frame_number_ <= kNumDebugFrames) {
    FX_VLOGS(1) << "RequestFrame";
  }

  const auto [new_target_presentation_time, new_wakeup_time] =
      ComputePresentationAndWakeupTimesForTargetTime(requested_presentation_time);

  TRACE_DURATION("gfx", "DefaultFrameScheduler::RequestFrame", "requested presentation time",
                 requested_presentation_time.get() / 1'000'000, "target_presentation_time",
                 new_target_presentation_time.get() / 1'000'000, "candidate wakeup time",
                 new_wakeup_time.get() / 1'000'000, "current wakeup time",
                 wakeup_time_.get() / 1'000'000);

  // If there is no render waiting we should schedule a frame. Likewise, if newly predicted wake up
  // time is earlier than the current one then we need to reschedule the next wake-up.
  if (!frame_render_task_.is_pending() || new_wakeup_time < wakeup_time_) {
    frame_render_task_.Cancel();

    wakeup_time_ = new_wakeup_time;
    next_target_presentation_time_ = new_target_presentation_time;
    frame_render_task_.PostForTime(dispatcher_, wakeup_time_);
  }
}

void DefaultFrameScheduler::HandleNextFrameRequest() {
  // Finds and requests a frame for the lowest requested_presentation_time across all sessions'
  // next update.
  if (!pending_present_requests_.empty()) {
    SessionId last_session = scheduling::kInvalidSessionId;
    zx::time next_min_time = zx::time(std::numeric_limits<zx_time_t>::max());
    for (const auto& [id_pair, request] : pending_present_requests_) {
      if (id_pair.session_id != last_session &&
          sessions_with_unsquashable_updates_pending_presentation_.count(id_pair.session_id) == 0) {
        last_session = id_pair.session_id;
        next_min_time = std::min(next_min_time, request.requested_presentation_time);
      }
    }

    if (next_min_time.get() != std::numeric_limits<zx_time_t>::max()) {
      RequestFrame(next_min_time);
    }
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
  if (frame_number < kNumDebugFrames) {
    FX_VLOGS(1) << "MaybeRenderFrame target_presentation_time=" << target_presentation_time.get()
                << " wakeup_time=" << wakeup_time_.get() << " frame_number=" << frame_number;
  }

  // Apply all updates
  const zx::time update_start_time = zx::time(async_now(dispatcher_));

  // The second value, |wakeup_time_|, here is important for ensuring our flows stay connected.
  // If you change it please ensure the "request_to_render" flow stays connected.
  const bool needs_render = ApplyUpdates(target_presentation_time, wakeup_time_, frame_number);

  if (needs_render) {
    inspect_last_successful_update_start_time_.Set(update_start_time.get());
    last_successful_update_start_time_ = update_start_time;
  }

  // TODO(fxbug.dev/24669) Revisit how we do this.
  const zx::time update_end_time = zx::time(async_now(dispatcher_));
  const zx::time render_start_time = update_end_time;
  frame_predictor_->ReportUpdateDuration(zx::duration(update_end_time - update_start_time));

  if (!needs_render && last_frame_is_presented_ && !render_continuously_) {
    inspect_wakeups_without_render_.Set(++wakeups_without_render_);

    // Nothing to render. Continue with next request in the queue.
    HandleNextFrameRequest();
    return;
  }

  // TODO(fxbug.dev/24531) Remove the presentation check, and pipeline frames within a VSYNC
  // interval.
  FX_DCHECK(last_presented_frame_number_ <= frame_number);
  // Only one frame is allowed "in flight" at any given. Don't start rendering another frame until
  // the previous frame is on the display.
  if (last_presented_frame_number_ < (frame_number - 1)) {
    last_frame_is_presented_ = false;
    return;
  }

  last_frame_is_presented_ = true;

  // Logging the first few frames to find common startup bugs.
  if (frame_number < kNumDebugFrames) {
    FX_LOGS(INFO) << "Calling RenderFrame target_presentation_time="
                  << target_presentation_time.get() << " frame_number=" << frame_number;
  }

  TRACE_INSTANT("gfx", "Render start", TRACE_SCOPE_PROCESS, "Expected presentation time",
                target_presentation_time.get(), "frame_number", frame_number);

  const trace_flow_id_t frame_render_trace_id = TRACE_NONCE();
  TRACE_FLOW_BEGIN("gfx", "render_to_presented", frame_render_trace_id);
  auto on_presented_callback = [=, weak = weak_factory_.GetWeakPtr()](
                                   const FrameRenderer::Timestamps& timestamps) {
    TRACE_FLOW_END("gfx", "render_to_presented", frame_render_trace_id);
    if (weak) {
      weak->OnFramePresented(frame_number, render_start_time, target_presentation_time, timestamps);
    } else {
      FX_LOGS(ERROR) << "Error, cannot record presentation time: FrameScheduler does not exist";
    }
  };
  outstanding_latch_points_.push_back(update_end_time);

  inspect_frame_number_.Set(frame_number);

  // Render the frame.
  if (auto renderer = frame_renderer_.lock()) {
    renderer->RenderScheduledFrame(frame_number, target_presentation_time,
                                   std::move(on_presented_callback));
  }

  ++frame_number_;

  // Let all Session Updaters know of the timing of the end of RenderFrame().
  for (auto updater : session_updaters_) {
    if (auto locked = updater.lock()) {
      locked->OnCpuWorkDone();
    }
  }

  // Schedule next frame if any unhandled presents are left.
  HandleNextFrameRequest();
}

void DefaultFrameScheduler::ScheduleUpdateForSession(zx::time requested_presentation_time,
                                                     SchedulingIdPair id_pair, bool squashable) {
  FX_DCHECK(id_pair.session_id != scheduling::kInvalidSessionId);
  TRACE_DURATION("gfx", "DefaultFrameScheduler::ScheduleUpdateForSession",
                 "requested_presentation_time", requested_presentation_time.get() / 1'000'000);

  TRACE_FLOW_END("gfx", "ScheduleUpdate", id_pair.present_id);

  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < kNumDebugFrames) {
    FX_VLOGS(1) << "ScheduleUpdateForSession session_id: " << id_pair.session_id
                << " requested_presentation_time: " << requested_presentation_time.get();
  }

  const trace_flow_id_t flow_id = TRACE_NONCE();
  TRACE_FLOW_BEGIN("gfx", "request_to_render", flow_id);
  pending_present_requests_.emplace(std::make_pair(
      id_pair, PresentRequest{.requested_presentation_time = requested_presentation_time,
                              .flow_id = flow_id,
                              .squashable = squashable}));

  HandleNextFrameRequest();
}

void DefaultFrameScheduler::GetFuturePresentationInfos(
    zx::duration requested_prediction_span,
    FrameScheduler::GetFuturePresentationInfosCallback presentation_infos_callback) {
  std::vector<FuturePresentationInfo> infos;

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
    infos.push_back(
        {.latch_point = times.latch_point_time, .presentation_time = times.presentation_time});

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

void DefaultFrameScheduler::OnFramePresented(uint64_t frame_number, zx::time render_start_time,
                                             zx::time target_presentation_time,
                                             const FrameRenderer::Timestamps& timestamps) {
  FX_DCHECK(frame_number == last_presented_frame_number_ + 1);
  FX_DCHECK(vsync_timing_->vsync_interval().get() >= 0);

  if (frame_number < kNumDebugFrames) {
    FX_LOGS(INFO) << "DefaultFrameScheduler::OnFramePresented"
                  << " frame_number=" << frame_number;
  }

  last_presented_frame_number_ = frame_number;

  FrameStats::Timestamps frame_stats = {
      .latch_point_time = outstanding_latch_points_.front(),
      .render_start_time = render_start_time,
      .render_done_time = timestamps.render_done_time,
      .target_presentation_time = target_presentation_time,
      .actual_presentation_time = timestamps.actual_presentation_time,
  };

  stats_.RecordFrame(frame_stats, vsync_timing_->vsync_interval());

  if (timestamps.render_done_time != FrameRenderer::kTimeDropped) {
    zx::duration duration =
        std::max(timestamps.render_done_time - render_start_time, zx::duration(0));
    frame_predictor_->ReportRenderDuration(zx::duration(duration));
    inspect_last_successful_render_start_time_.Set(target_presentation_time.get());
    last_successful_render_start_time_ = target_presentation_time;
  }

  if (timestamps.actual_presentation_time == FrameRenderer::kTimeDropped) {
    TRACE_INSTANT("gfx", "FrameDropped", TRACE_SCOPE_PROCESS, "frame_number", frame_number);
  } else {
    if (TRACE_CATEGORY_ENABLED("gfx")) {
      // Log trace data..
      zx::duration target_vs_actual =
          timestamps.actual_presentation_time - target_presentation_time;

      zx::time now = zx::time(async_now(dispatcher_));
      zx::duration elapsed_since_presentation = now - timestamps.actual_presentation_time;
      FX_DCHECK(elapsed_since_presentation.get() >= 0);

      TRACE_INSTANT("gfx", "FramePresented", TRACE_SCOPE_PROCESS, "frame_number", frame_number,
                    "presentation time", timestamps.actual_presentation_time.get(),
                    "target time missed by", target_vs_actual.get(),
                    "elapsed time since presentation", elapsed_since_presentation.get());
    }

    SignalPresentedUpTo(frame_number, /*presentation_time*/ timestamps.actual_presentation_time,
                        /*presentation_interval*/ vsync_timing_->vsync_interval());
  }
  outstanding_latch_points_.pop_front();

  sessions_with_unsquashable_updates_pending_presentation_.clear();

  if (!last_frame_is_presented_ || render_continuously_) {
    RequestFrame(zx::time(0));
  } else {
    // Schedule next frame if any unhandled presents are left.
    HandleNextFrameRequest();
  }
}

void DefaultFrameScheduler::RemoveSession(SessionId session_id) {
  RemoveSessionIdFromMap(session_id, &presents_);
  RemoveSessionIdFromMap(session_id, &pending_present_requests_);
  RemoveSessionIdFromMap(session_id, &release_fences_);
}

std::unordered_map<SessionId, PresentId> DefaultFrameScheduler::CollectUpdatesForThisFrame(
    zx::time target_presentation_time) {
  std::unordered_map<SessionId, PresentId> updates;

  SessionId current_session = scheduling::kInvalidSessionId;
  bool hit_limit = false;
  bool preceding_update_is_squashable = true;
  auto it = pending_present_requests_.begin();
  while (it != pending_present_requests_.end()) {
    auto& [id_pair, present_request] = *it;

    if (current_session != id_pair.session_id) {
      current_session = id_pair.session_id;
      hit_limit = false;
      preceding_update_is_squashable = true;
    }

    if (!hit_limit && present_request.requested_presentation_time <= target_presentation_time &&
        preceding_update_is_squashable &&
        sessions_with_unsquashable_updates_pending_presentation_.count(id_pair.session_id) == 0) {
      TRACE_FLOW_END("gfx", "request_to_render", present_request.flow_id);
      // Return only the last relevant present id for each session.
      updates[current_session] = id_pair.present_id;
      if (!present_request.squashable) {
        sessions_with_unsquashable_updates_pending_presentation_.emplace(id_pair.session_id);
      }

      preceding_update_is_squashable = present_request.squashable;
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
  std::vector<zx::event> fences;

  for (const auto& [session_id, present_id] : updates) {
    SetLatchedTimeForPresentsUpTo({session_id, present_id}, latched_time);

    const auto begin_it = release_fences_.lower_bound({session_id, 0});
    const auto end_it = release_fences_.lower_bound({session_id, present_id});
    FX_DCHECK(std::distance(begin_it, end_it) >= 0);
    std::for_each(
        begin_it, end_it,
        [&fences](std::pair<const SchedulingIdPair, std::vector<zx::event>>& release_fences) {
          std::move(std::begin(release_fences.second), std::end(release_fences.second),
                    std::back_inserter(fences));
        });
    release_fences_.erase(begin_it, end_it);
  }

  if (auto renderer = frame_renderer_.lock()) {
    renderer->SignalFencesWhenPreviousRendersAreDone(std::move(fences));
  }
}

SessionUpdater::UpdateResults DefaultFrameScheduler::ApplyUpdatesToEachUpdater(
    const std::unordered_map<SessionId, PresentId>& sessions_to_update, uint64_t frame_number) {
  // Apply updates to each SessionUpdater.
  SessionUpdater::UpdateResults update_results;
  std::for_each(
      session_updaters_.begin(), session_updaters_.end(),
      [&sessions_to_update, &update_results,
       frame_number](const std::weak_ptr<SessionUpdater>& updater) {
        if (auto locked_updater = updater.lock()) {
          // Aggregate results from each updater.
          // Note: Currently, only one SessionUpdater handles each SessionId. If this
          // changes, then a SessionId corresponding to a failed update should not be
          // passed to subsequent SessionUpdaters.
          update_results.merge(locked_updater->UpdateSessions(sessions_to_update, frame_number));
        }
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

bool DefaultFrameScheduler::ApplyUpdates(zx::time target_presentation_time, zx::time latched_time,
                                         uint64_t frame_number) {
  FX_DCHECK(latched_time <= target_presentation_time);

  // Logging the first few frames to find common startup bugs.
  if (frame_number < kNumDebugFrames) {
    FX_VLOGS(1) << "ApplyScheduledSessionUpdates target_presentation_time="
                << target_presentation_time.get() << " frame_number=" << frame_number;
  }

  // NOTE: this name is used by scenic_frame_stats.dart
  TRACE_DURATION("gfx", "ApplyScheduledSessionUpdates", "target_presentation_time",
                 target_presentation_time.get() / 1'000'000, "frame_number", frame_number);

  TRACE_FLOW_BEGIN("gfx", "scenic_frame", frame_number);

  const auto update_map = CollectUpdatesForThisFrame(target_presentation_time);
  const bool have_updates = !update_map.empty();
  PrepareUpdates(update_map, latched_time, frame_number);
  const auto update_results = ApplyUpdatesToEachUpdater(update_map, frame_number);
  RemoveFailedSessions(update_results.sessions_with_failed_updates);

  // If anything was updated, we need to render.
  return have_updates;
}

void DefaultFrameScheduler::RemoveFailedSessions(
    const std::unordered_set<SessionId>& sessions_with_failed_updates) {
  for (auto failed_session_id : sessions_with_failed_updates) {
    RemoveSession(failed_session_id);
  }
}

void DefaultFrameScheduler::SignalPresentedUpTo(uint64_t frame_number, zx::time presentation_time,
                                                zx::duration presentation_interval) {
  // Get last present_id up to |frame_number| for each session.
  std::unordered_map<SessionId, PresentId> last_updates;
  std::unordered_map<SessionId, std::map<PresentId, zx::time>> latched_times;
  while (!latched_updates_.empty() && latched_updates_.front().frame_number <= frame_number) {
    for (const auto& [session_id, present_id] : latched_updates_.front().updated_sessions) {
      last_updates[session_id] = present_id;
    }
    latched_updates_.pop();
  }

  for (const auto& [session_id, present_id] : last_updates) {
    latched_times[session_id] = ExtractLatchTimestampsUpTo({session_id, present_id});
  }

  const PresentTimestamps present_timestamps{
      .presented_time = zx::time(presentation_time),
      .vsync_interval = zx::duration(presentation_interval),
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

void DefaultFrameScheduler::LogPeriodicDebugInfo() {
  FX_LOGS(INFO) << "DefaultFrameScheduler::LogPeriodicDebugInfo()"
                << "\n\t frame number: " << frame_number_
                << "\n\t current time: " << async_now(dispatcher_)
                << "\n\t last successful update start time: "
                << last_successful_update_start_time_.get()
                << "\n\t last successful render start time: "
                << last_successful_render_start_time_.get();
}

}  // namespace scheduling
