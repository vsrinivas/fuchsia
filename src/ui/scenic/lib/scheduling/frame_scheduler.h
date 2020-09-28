// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_FRAME_SCHEDULER_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_FRAME_SCHEDULER_H_

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/scenic/scheduling/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/scheduling/frame_timings.h"
#include "src/ui/scenic/lib/scheduling/id.h"
#include "src/ui/scenic/lib/scheduling/present2_info.h"

namespace scheduling {

// Callback used for Present1 and ImagePipe::Present
using OnPresentedCallback = fit::function<void(fuchsia::images::PresentationInfo)>;
// Callback used for Present2.
using OnFramePresentedCallback =
    fit::function<void(fuchsia::scenic::scheduling::FramePresentedInfo info)>;

struct PresentTimestamps {
  zx::time presented_time = zx::time(0);
  zx::duration vsync_interval = zx::duration(0);
};

// Interface for performing session updates.
class SessionUpdater {
 public:
  // Returned by |UpdateSessions()|.
  struct UpdateResults {
    // SessionIds whose updates failed.
    std::unordered_set<SessionId> sessions_with_failed_updates;
  };

  virtual ~SessionUpdater() = default;

  // For each known session in |sessions_to_update|, apply all updates up to and including
  // |PresentId|.
  virtual UpdateResults UpdateSessions(
      const std::unordered_map<SessionId, PresentId>& sessions_to_update, uint64_t trace_id) = 0;

  // Called whenever a new set of presents have been presented to the screen. |latched_times| gives
  // information about when each individual update was latched.
  virtual void OnFramePresented(
      const std::unordered_map<SessionId, std::map<PresentId, /*latched_time*/ zx::time>>&
          latched_times,
      PresentTimestamps present_times) = 0;
};

// Result of a call to FrameRenderer::RenderFrame(). See below.
enum RenderFrameResult { kRenderSuccess, kRenderFailed, kNoContentToRender };

// Interface for rendering frames.
class FrameRenderer {
 public:
  virtual ~FrameRenderer() = default;

  // Called when it's time to render a new frame.  The FrameTimings object is used to accumulate
  // timing for all swapchains that are used as render targets in that frame.
  //
  // If RenderFrame() returns true, the delegate is responsible for calling
  // FrameTimings::OnFrameRendered/Presented/Dropped(). Otherwise, rendering did not occur for some
  // reason, and the FrameScheduler should not expect to receive any timing information for that
  // frame.
  // TODO(fxbug.dev/24297): these return value semantics are not ideal.  See comments in
  // Engine::RenderFrame() regarding this same issue.
  virtual RenderFrameResult RenderFrame(fxl::WeakPtr<FrameTimings> frame_timings,
                                        zx::time presentation_time) = 0;
};

// The FrameScheduler is responsible for scheduling frames to be drawn in response to requests from
// clients.  When a frame is requested, the FrameScheduler will decide at which Vsync the frame
// should be displayed at. This time will be no earlier than the requested time, and will be as
// close as possible to the requested time, subject to various constraints.  For example, if the
// requested time is earlier than the time that rendering would finish, were it started immediately,
// then the frame will be scheduled for a later Vsync.
class FrameScheduler {
 public:
  virtual ~FrameScheduler() = default;

  // Set the renderer that will be used to render frames.  Can be set exactly once.  Must be set
  // before any frames are rendered.
  virtual void SetFrameRenderer(std::weak_ptr<FrameRenderer> frame_renderer) = 0;

  // Add a session updater to the FrameScheduler.  This is safe to do between frames (i.e. not while
  // sessions are being updated before a frame is rendered).
  virtual void AddSessionUpdater(std::weak_ptr<SessionUpdater> session_updater) = 0;

  // If |render_continuously|, we keep scheduling new frames immediately after each presented frame,
  // regardless of whether they're explicitly requested using RequestFrame().
  virtual void SetRenderContinuously(bool render_continuously) = 0;

  // Registers per-present information with the frame scheduler and returns an incrementing
  // PresentId unique to that session. The |present_id| argument should only be set when
  // transferring sessions between frame schedulers.
  virtual PresentId RegisterPresent(
      SessionId session_id, std::variant<OnPresentedCallback, Present2Info> present_information,
      std::vector<zx::event> release_fences, PresentId present_id = 0) = 0;

  // Tell the FrameScheduler to schedule a frame. This is also used for updates triggered by
  // something other than a Session update i.e. an ImagePipe with a new Image to present.
  virtual void ScheduleUpdateForSession(zx::time presentation_time, SchedulingIdPair id_pair) = 0;

  // Gets the predicted latch points and presentation times for the frames at or before the next
  // |requested_prediction_span| time span. Uses the FramePredictor to do so.
  using GetFuturePresentationInfosCallback =
      fit::function<void(std::vector<fuchsia::scenic::scheduling::PresentationInfo>)>;
  virtual void GetFuturePresentationInfos(zx::duration requested_prediction_span,
                                          GetFuturePresentationInfosCallback callback) = 0;

  // Sets the |fuchsia::ui::scenic::Session::OnFramePresented| event handler. This should only be
  // called once per session.
  virtual void SetOnFramePresentedCallbackForSession(SessionId session,
                                                     OnFramePresentedCallback callback) = 0;

  // Removes all references to |session_id|.
  virtual void RemoveSession(SessionId session_id) = 0;

  // Clients cannot call Present() anymore when |presents_in_flight_| reaches this value. Scenic
  // uses this to apply backpressure to clients.
  // TODO(fxbug.dev/44211): Move into implementation.
  static constexpr int64_t kMaxPresentsInFlight = 5;
};

}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_FRAME_SCHEDULER_H_
