// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_FRAME_SCHEDULER_H_
#define GARNET_LIB_UI_GFX_ENGINE_FRAME_SCHEDULER_H_

#include <queue>

#include <lib/async/dispatcher.h>
#include <lib/zx/time.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace scenic {
namespace gfx {

class Display;
class FrameTimings;
using FrameTimingsPtr = fxl::RefPtr<FrameTimings>;

// Interface implemented by the engine to perform per-frame processing in
// response to a frame being scheduled.
class FrameSchedulerDelegate {
 public:
  virtual ~FrameSchedulerDelegate() = default;

  // Called when it's time to apply changes to the scene graph and render
  // a new frame.  The FrameTimings object is used to accumulate timing
  // for all swapchains that are used as render targets in that frame.
  //
  // TODO(MZ-225): We need to track backpressure so that the frame scheduler
  // doesn't get too far ahead. With that in mind, Renderer::DrawFrame should
  // have a callback which is invoked when the frame is fully flushed through
  // the graphics pipeline. Then Engine::RenderFrame itself should have a
  // callback which is invoked when all renderers finish work for that frame.
  // Then FrameScheduler should listen to the callback to count how many
  // frames are in flight and back off.
  virtual bool RenderFrame(const FrameTimingsPtr& frame_timings,
                           uint64_t presentation_time,
                           uint64_t presentation_interval,
                           bool force_render) = 0;
};

// The FrameScheduler is responsible for scheduling frames to be drawn in
// response to requests from clients.  When a frame is requested, the
// FrameScheduler will decide at which Vsync the frame should be displayed at.
// This time will be no earlier than the requested time, and will be as close
// as possible to the requested time, subject to various constraints.  For
// example, if the requested time is earlier than the time that rendering would
// finish, were it started immediately, then the frame will be scheduled for a
// later Vsync.
class FrameScheduler {
 public:
  explicit FrameScheduler(Display* display);
  ~FrameScheduler();

  void set_delegate(FrameSchedulerDelegate* delegate) { delegate_ = delegate; }

  // Request a frame to be scheduled at or after |presentation_time|, which
  // may be in the past.
  void RequestFrame(zx_time_t presentation_time);

  // If |render_continuously|, we keep rendering frames regardless of whether
  // they're requested using RequestFrame().
  void SetRenderContinuously(bool render_continuously);

  // Helper method for ScheduleFrame().  Returns the target presentation time
  // for the requested presentation time, and a wake-up time that is early
  // enough to start rendering in order to hit the target presentation time.
  std::pair<zx_time_t, zx_time_t> ComputeTargetPresentationAndWakeupTimes(
      zx_time_t requested_presentation_time) const;

 private:
  // Update the global scene and then draw it... maybe.  There are multiple
  // reasons why this might not happen.  For example, the swapchain might apply
  // back-pressure if we can't hit our target frame rate.  Or, after this frame
  // was scheduled, another frame was scheduled to be rendered at an earlier
  // time, and not enough time has elapsed to render this frame.  Etc.
  void MaybeRenderFrame(zx_time_t presentation_time, zx_time_t wakeup_time);

  // Schedule a frame for the earliest of |requested_presentation_times_|.  The
  // scheduled time will be the earliest achievable time, such that rendering
  // can start early enough to hit the next Vsync.
  void ScheduleFrame();

  // Returns true to apply back-pressure when we cannot hit our target frame
  // rate.  Otherwise, return false to indicate that it is OK to immediately
  // render a frame.
  bool TooMuchBackPressure();

  // Helper method for ScheduleFrame().  Returns the target presentation time
  // for the next frame, and a wake-up time that is early enough to start
  // rendering in order to hit the target presentation time.
  std::pair<zx_time_t, zx_time_t> ComputeNextPresentationAndWakeupTimes() const;

  // Return the predicted amount of time required to render a frame.
  zx_time_t PredictRequiredFrameRenderTime() const;

  // Called by the delegate when the frame drawn by RenderFrame() has been
  // presented to the display.
  friend class FrameTimings;
  void OnFramePresented(FrameTimings* timings);

  async_dispatcher_t* const dispatcher_;
  FrameSchedulerDelegate* delegate_;
  Display* const display_;

  std::priority_queue<zx_time_t, std::vector<zx_time_t>,
                      std::greater<zx_time_t>>
      requested_presentation_times_;

  uint64_t frame_number_ = 0;
  constexpr static size_t kMaxOutstandingFrames = 2;
  std::vector<FrameTimingsPtr> outstanding_frames_;
  bool back_pressure_applied_ = false;
  bool render_continuously_ = false;

  fxl::WeakPtrFactory<FrameScheduler> weak_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(FrameScheduler);
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_ENGINE_FRAME_SCHEDULER_H_
