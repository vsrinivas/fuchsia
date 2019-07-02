// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_FRAME_SCHEDULER_H_
#define GARNET_LIB_UI_GFX_ENGINE_FRAME_SCHEDULER_H_

#include <fuchsia/images/cpp/fidl.h>
#include <lib/zx/time.h>
#include <src/lib/fxl/memory/weak_ptr.h>

#include <queue>
#include <unordered_set>

#include "garnet/lib/ui/gfx/id.h"

namespace scenic_impl {
namespace gfx {

class FrameTimings;
using FrameTimingsPtr = fxl::RefPtr<FrameTimings>;

using PresentationInfo = fuchsia::images::PresentationInfo;
using OnPresentedCallback = fit::function<void(PresentationInfo)>;

// Interface for performing session updates.
class SessionUpdater {
 public:
  // Returned by |UpdateSessions()|.
  struct UpdateResults {
    // Indicates that a frame needs to be rendered.  This is typically due to modification of the
    // scene graph due to an applied update, but can be for other reasons.
    bool needs_render = false;
    // A list of sessions that need to be rescheduled, for example because not all of their acquire
    // fences were signaled before |UpdateSessions()| was called.
    std::unordered_set<SessionId> sessions_to_reschedule;
    // A list of callbacks that should be invoked once the rendered frame is presented (or if the
    // frame is dropped, once the next frame is presented).
    std::queue<OnPresentedCallback> present_callbacks;
  };

  virtual ~SessionUpdater() = default;

  // For each known session in |sessions_to_update|, apply all of the "ready" updates.  A "ready"
  // update is one that is scheduled at or before |presentation_time|, and for which all other
  // preconditions have been met (for example, all acquire fences have been signaled).
  virtual UpdateResults UpdateSessions(std::unordered_set<SessionId> sessions_to_update,
                                       zx_time_t presentation_time, uint64_t trace_id) = 0;

  // Notify updater that no more sessions will be updated before rendering the next frame; now is
  // the time to do any necessary work before the frame is rendered.  For example, animations might
  // be run now.
  virtual void PrepareFrame(zx_time_t presentation_time, uint64_t trace_id) = 0;

  // Helper function to move callbacks from one queue to another.
  static void MoveCallbacksFromTo(std::queue<OnPresentedCallback>* src,
                                  std::queue<OnPresentedCallback>* dst);
};

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
  // TODO(SCN-1089): these return value semantics are not ideal.  See comments in
  // Engine::RenderFrame() regarding this same issue.
  virtual bool RenderFrame(const FrameTimingsPtr& frame_timings, zx_time_t presentation_time) = 0;
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
  virtual void SetFrameRenderer(fxl::WeakPtr<FrameRenderer> frame_renderer) = 0;

  // Add a session updater to the FrameScheduler.  This is safe to do between frames (i.e. not while
  // sessions are being updated before a frame is rendered).
  virtual void AddSessionUpdater(fxl::WeakPtr<SessionUpdater> session_updater) = 0;

  // If |render_continuously|, we keep scheduling new frames immediately after each presented frame,
  // regardless of whether they're explicitly requested using RequestFrame().
  virtual void SetRenderContinuously(bool render_continuously) = 0;

  // Tell the FrameScheduler to schedule a frame. This is also used for updates triggered by
  // something other than a Session update i.e. an ImagePipe with a new Image to present.
  virtual void ScheduleUpdateForSession(zx_time_t presentation_time,
                                        scenic_impl::SessionId session) = 0;

 protected:
  friend class FrameTimings;

  // Called when the frame drawn by RenderFrame() has been presented to the display.
  virtual void OnFramePresented(const FrameTimings& timings) = 0;

  // Called when the frame drawn by RenderFrame() has finished rendering.
  virtual void OnFrameRendered(const FrameTimings& timings) = 0;
};

// Inline function definitions.

inline void SessionUpdater::MoveCallbacksFromTo(std::queue<OnPresentedCallback>* src,
                                                std::queue<OnPresentedCallback>* dst) {
  while (!src->empty()) {
    dst->push(std::move(src->front()));
    src->pop();
  }
}

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_FRAME_SCHEDULER_H_
