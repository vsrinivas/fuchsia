// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/default_frame_scheduler.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/async/time.h>
#include <trace/event.h>
#include <zircon/syscalls.h>

#include "garnet/lib/ui/gfx/displays/display.h"
#include "garnet/lib/ui/gfx/engine/frame_timings.h"
#include "src/lib/fxl/logging.h"

namespace scenic_impl {
namespace gfx {

DefaultFrameScheduler::DefaultFrameScheduler(const Display* display)
    : dispatcher_(async_get_default_dispatcher()),
      display_(display),
      weak_factory_(this) {
  outstanding_frames_.reserve(kMaxOutstandingFrames);
}

DefaultFrameScheduler::~DefaultFrameScheduler() {}

void DefaultFrameScheduler::OnFrameRendered(const FrameTimings& timings) {
  TRACE_INSTANT("gfx", "DefaultFrameScheduler::OnFrameRendered",
                TRACE_SCOPE_PROCESS, "Timestamp",
                timings.rendering_finished_time(), "Frame number",
                timings.frame_number());
}

void DefaultFrameScheduler::SetRenderContinuously(bool render_continuously) {
  render_continuously_ = render_continuously;
  if (render_continuously_) {
    RequestFrame();
  }
}

zx_time_t DefaultFrameScheduler::PredictRequiredFrameRenderTime() const {
  // TODO(MZ-400): more sophisticated prediction.  This might require more info,
  // e.g. about how many compositors will be rendering scenes, at what
  // resolutions, etc.
  constexpr zx_time_t kHardcodedPrediction = 8'000'000;  // 8ms
  return kHardcodedPrediction;
}

std::pair<zx_time_t, zx_time_t>
DefaultFrameScheduler::ComputePresentationAndWakeupTimesForTargetTime(
    const zx_time_t requested_presentation_time) const {
  const zx_time_t last_vsync_time = display_->GetLastVsyncTime();
  const zx_duration_t vsync_interval = display_->GetVsyncInterval();
  const zx_time_t now = async_now(dispatcher_);
  const zx_duration_t required_render_time = PredictRequiredFrameRenderTime();

  // Compute the number of full vsync intervals between the last vsync and the
  // requested presentation time.  Notes:
  //   - The requested time might be earlier than the last vsync time,
  //     for example when client content is a bit late.
  //   - We subtract a nanosecond before computing the number of intervals, to
  //     avoid an off-by-one error in the common case where a client computes a
  //     a desired presentation time based on a previously-received actual
  //     presentation time.
  uint64_t num_intervals =
      1 + (requested_presentation_time <= last_vsync_time
               ? 0
               : (requested_presentation_time - last_vsync_time - 1) /
                     vsync_interval);

  // Compute the target vsync/presentation time, and the time we would need to
  // start rendering to meet the target.
  zx_time_t target_presentation_time =
      last_vsync_time + (num_intervals * vsync_interval);
  zx_time_t wakeup_time = target_presentation_time - required_render_time;
  // Handle startup-time corner case: since monotonic clock starts at 0, there
  // will be underflow when required_render_time > target_presentation_time,
  // resulting in a *very* late wakeup time.
  while (required_render_time > target_presentation_time) {
    target_presentation_time += vsync_interval;
    wakeup_time = target_presentation_time - required_render_time;
  }

  // If it's too late to start rendering, drop a frame.
  while (wakeup_time < now) {
    // TODO(MZ-400): This is insufficient.  It prevents Scenic from
    // overcommitting but it doesn't prevent apparent jank.  For example,
    // consider apps like hello_scenic that don't render the next frame
    // until they receive the async response to the present call, which contains
    // the actual presentation time.  Currently, it won't receive that response
    // until the defered frame is rendered, and so the animated content will be
    // at the wrong position.
    //
    // One solution is to evaluate animation inside Scenic.  However, there will
    // probably always be apps that use the "hello_scenic pattern".  To
    // support this, the app needs to be notified of the dropped frame so that
    // it can make any necessary updates before the next frame is rendered.
    //
    // This seems simple enough, but it is tricky to specify/implement:
    //
    // It is critical to maintain the invariant that receiving the Present()
    // response means that your commands enqueued before that Present() were
    // actually applied in the session.  Currently, we only do this when
    // rendering a frame, but we would have to do it earlier.  Also, is this
    // even the right invariant? See discussion in session.fidl
    //
    // But when do we do it?  Immediately?  That might be well before the
    // desired presentation time; is that a problem?  Do we sleep twice, once to
    // wake up and apply commands without rendering, and again to render?
    //
    // If we do that, what presentation time should we return to clients, given
    // that our only access to Vsync times is via an event signaled by Magma?
    // (probably we could just extrapolate from the previous vsync, assuming
    // that there is one, but this is nevertheless complexity to consider).
    //
    // Other complications will arise as when we try to address this.
    // Try it and see!

    // Drop a frame.
    target_presentation_time += vsync_interval;
    wakeup_time += vsync_interval;
  }

#if SCENIC_IGNORE_VSYNC
  return std::make_pair(now, now);
#else
  return std::make_pair(target_presentation_time, wakeup_time);
#endif
}

void DefaultFrameScheduler::RequestFrame() {
  FXL_CHECK(!updatable_sessions_.empty() || render_continuously_ ||
            render_pending_);

  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 5) {
    FXL_LOG(INFO) << "DefaultFrameScheduler::RequestFrame";
  }

  auto requested_presentation_time =
      render_continuously_ || render_pending_
          ? 0
          : updatable_sessions_.top().requested_presentation_time;

  auto next_times = ComputePresentationAndWakeupTimesForTargetTime(
      requested_presentation_time);
  auto new_presentation_time = next_times.first;
  auto new_wakeup_time = next_times.second;

  // If there is no render waiting we should schedule a frame.
  // Likewise, if newly predicted wake up time is earlier than the current one
  // then we need to reschedule the next wake up.
  if (!frame_render_task_.is_pending() || new_wakeup_time < wakeup_time_) {
    frame_render_task_.Cancel();

    wakeup_time_ = new_wakeup_time;
    next_presentation_time_ = new_presentation_time;
    frame_render_task_.PostForTime(dispatcher_, zx::time(wakeup_time_));
  }
}

void DefaultFrameScheduler::MaybeRenderFrame(async_dispatcher_t*,
                                             async::TaskBase*, zx_status_t) {
  auto presentation_time = next_presentation_time_;
  TRACE_DURATION("gfx", "FrameScheduler::MaybeRenderFrame", "presentation_time",
                 presentation_time);

  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 5) {
    FXL_LOG(INFO)
        << "DefaultFrameScheduler::MaybeRenderFrame presentation_time="
        << presentation_time << " wakeup_time=" << wakeup_time_
        << " frame_number=" << frame_number_;
  }

  // TODO(MZ-400): Check whether there is enough time to render.  If not, drop
  // a frame and reschedule.  It would be nice to simply bump the presentation
  // time, but then there is a danger of rendering too fast, and actually
  // presenting 1 vsync before the bumped presentation time.  The safest way to
  // avoid this is to start rendering after the vsync that we want to skip.
  //
  // TODO(MZ-400): If there isn't enough time to render, why did this happen?
  // One possiblity is that we woke up later than expected, and another is an
  // increase in the estimated time required to render the frame (well, not
  // currently: the estimate is a hard-coded constant.  Figure out what
  // happened, and log it appropriately.

  // TODO(SCN-124): We should derive an appropriate value from the rendering
  // targets, in particular giving priority to couple to the display refresh
  // (vsync).
  auto presentation_interval = display_->GetVsyncInterval();
  if (delegate_.frame_renderer && delegate_.session_updater) {
    // Apply all updates
    bool any_updates_were_applied =
        ApplyScheduledSessionUpdates(presentation_time, presentation_interval);

    if (!any_updates_were_applied && !render_pending_ &&
        !render_continuously_) {
      return;
    }

    if (currently_rendering_) {
      render_pending_ = true;
      return;
    }

    FXL_DCHECK(outstanding_frames_.size() < kMaxOutstandingFrames);

    // Logging the first few frames to find common startup bugs.
    if (frame_number_ < 5) {
      FXL_LOG(INFO)
          << "DefaultFrameScheduler: calling RenderFrame presentation_time="
          << presentation_time << " frame_number=" << frame_number_;
    }

    TRACE_INSTANT("gfx", "Render start", TRACE_SCOPE_PROCESS, "Timestamp",
                  async_now(dispatcher_), "Frame number", frame_number_);

    auto frame_timings = fxl::MakeRefCounted<FrameTimings>(this, frame_number_,
                                                           presentation_time);

    ++frame_number_;

    // Render the frame.
    currently_rendering_ = delegate_.frame_renderer->RenderFrame(
        frame_timings, presentation_time, presentation_interval);
    if (currently_rendering_) {
      outstanding_frames_.push_back(frame_timings);
      render_pending_ = false;
    }
  }

  // If necessary, schedule another frame.
  if (!updatable_sessions_.empty()) {
    RequestFrame();
  }
}

void DefaultFrameScheduler::ScheduleUpdateForSession(
    zx_time_t presentation_time, scenic_impl::SessionId session_id) {
  updatable_sessions_.push({.session_id = session_id,
                            .requested_presentation_time = presentation_time});
  RequestFrame();
}

bool DefaultFrameScheduler::ApplyScheduledSessionUpdates(
    zx_time_t presentation_time, zx_duration_t presentation_interval) {
  FXL_DCHECK(delegate_.session_updater);

  // Logging the first few frames to find common startup bugs.
  if (frame_number_ < 5) {
    FXL_LOG(INFO) << "DefaultFrameScheduler::ApplyScheduledSessionUpdates "
                     "presentation_time="
                  << presentation_time << " frame_number=" << frame_number_;
  }
  TRACE_DURATION("gfx", "ApplyScheduledSessionUpdates", "time",
                 presentation_time, "interval", presentation_interval);

  std::vector<SessionUpdate> sessions_to_update;
  while (!updatable_sessions_.empty()) {
    auto& top = updatable_sessions_.top();

    if (top.requested_presentation_time > presentation_time) {
      break;
    }

    sessions_to_update.push_back(std::move(top));
    updatable_sessions_.pop();
  }

  bool needs_render = delegate_.session_updater->UpdateSessions(
      std::move(sessions_to_update), frame_number_, presentation_time,
      presentation_interval);

  return needs_render;
}

void DefaultFrameScheduler::OnFramePresented(const FrameTimings& timings) {
  FXL_DCHECK(!outstanding_frames_.empty());
  // TODO(MZ-400): how should we handle this case?  It is theoretically
  // possible, but if if it happens then it means that the EventTimestamper is
  // receiving signals out-of-order and is therefore generating bogus data.
  FXL_DCHECK(outstanding_frames_[0].get() == &timings) << "out-of-order.";

  if (timings.frame_was_dropped()) {
    TRACE_INSTANT("gfx", "FrameDropped", TRACE_SCOPE_PROCESS, "frame_number",
                  timings.frame_number());
  } else {
    // Log trace data.
    // TODO(MZ-400): just pass the whole Frame to a listener.
    int64_t target_vs_actual_usecs =
        static_cast<int64_t>(timings.actual_presentation_time() -
                             timings.target_presentation_time()) /
        1000;

    zx_time_t now = async_now(dispatcher_);
    FXL_DCHECK(now >= timings.actual_presentation_time());
    uint64_t elapsed_since_presentation_usecs =
        static_cast<int64_t>(now - timings.actual_presentation_time()) / 1000;

    TRACE_INSTANT("gfx", "FramePresented", TRACE_SCOPE_PROCESS, "frame_number",
                  timings.frame_number(), "presentation time (usecs)",
                  timings.actual_presentation_time() / 1000,
                  "target time missed by (usecs)", target_vs_actual_usecs,
                  "elapsed time since presentation (usecs)",
                  elapsed_since_presentation_usecs);
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

}  // namespace gfx
}  // namespace scenic_impl
