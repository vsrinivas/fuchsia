// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_DEFAULT_FRAME_SCHEDULER_H_
#define GARNET_LIB_UI_GFX_ENGINE_DEFAULT_FRAME_SCHEDULER_H_

#include <queue>

#include <lib/async/dispatcher.h>
#include <lib/zx/time.h>
#include "garnet/lib/ui/gfx/engine/frame_scheduler.h"
#include "garnet/lib/ui/gfx/id.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace scenic_impl {
namespace gfx {

using SessionUpdate = SessionUpdater::SessionUpdate;
class Display;

class DefaultFrameScheduler : public FrameScheduler {
 public:
  explicit DefaultFrameScheduler(Display* const display);
  ~DefaultFrameScheduler();

  // |FrameScheduler|
  void SetDelegate(FrameSchedulerDelegate delegate) override {
    delegate_ = delegate;
  };

  // |FrameScheduler|
  //
  // If |render_continuously|, we keep rendering frames regardless of whether
  // they're requested using RequestFrame().
  void SetRenderContinuously(bool render_continuously) override;

  // |FrameScheduler|
  //
  // Tell the FrameScheduler to schedule a frame. This is also used for
  // updates triggered by something other than a Session update i.e. an
  // ImagePipe with a new Image to present.
  void ScheduleUpdateForSession(uint64_t presentation_time,
                                scenic_impl::SessionId session) override;

 protected:
  // |FrameScheduler|
  void OnFramePresented(const FrameTimings& timings) override;

  // |FrameScheduler|
  void OnFrameRendered(const FrameTimings& timings) override;

 private:
  // Used to compare presentation times so that the priority_queue acts as a min
  // heap, placing the earliest PresentationTime at the top
  class UpdatableSessionsComparator {
   public:
    bool operator()(SessionUpdate updatable_session1,
                    SessionUpdate updatable_session2) {
      return updatable_session1.requested_presentation_time >
             updatable_session2.requested_presentation_time;
    }
  };

  // Request a frame to be scheduled at or after |presentation_time|, which
  // may be in the past.
  void RequestFrame(zx_time_t presentation_time);

  // Update the global scene and then draw it... maybe.  There are multiple
  // reasons why this might not happen.  For example, the swapchain might apply
  // back-pressure if we can't hit our target frame rate.  Or, after this frame
  // was scheduled, another frame was scheduled to be rendered at an earlier
  // time, and not enough time has elapsed to render this frame.  Etc.
  void MaybeRenderFrame(zx_time_t presentation_time, zx_time_t wakeup_time,
                        uint64_t flow_id);

  // Schedule a frame for the earliest of |requested_presentation_times_|.  The
  // scheduled time will be the earliest achievable time, such that rendering
  // can start early enough to hit the next Vsync.
  void ScheduleFrame();

  // Returns true to apply back-pressure when we cannot hit our target frame
  // rate.  Otherwise, return false to indicate that it is OK to immediately
  // render a frame.
  // TODO(MZ-225): We need to track backpressure so that the frame scheduler
  // doesn't get too far ahead. With that in mind, Renderer::DrawFrame should
  // have a callback which is invoked when the frame is fully flushed through
  // the graphics pipeline. Then Engine::RenderFrame itself should have a
  // callback which is invoked when all renderers finish work for that frame.
  // Then FrameScheduler should listen to the callback to count how many
  bool TooMuchBackPressure();

  // Helper method for ScheduleFrame().  Returns the target presentation time
  // for the next frame, and a wake-up time that is early enough to start
  // rendering in order to hit the target presentation time.
  std::pair<zx_time_t, zx_time_t> ComputeNextPresentationAndWakeupTimes() const;
  // Computes the target presentation time for the requested presentation time.
  std::pair<zx_time_t, zx_time_t> ComputeTargetPresentationAndWakeupTimes(
      zx_time_t requested_presentation_time) const;

  // Return the predicted amount of time required to render a frame.
  zx_time_t PredictRequiredFrameRenderTime() const;

  // Executes updates that are scheduled up to and including a given
  // presentation time. Returns true if rendering is needed.
  bool ApplyScheduledSessionUpdates(uint64_t frame_number,
                                    uint64_t presentation_time,
                                    uint64_t presentation_interval);

  async_dispatcher_t* const dispatcher_;
  const Display* const display_;

  std::priority_queue<zx_time_t, std::vector<zx_time_t>,
                      std::greater<zx_time_t>>
      requested_presentation_times_;

  uint64_t frame_number_ = 0;
  constexpr static size_t kMaxOutstandingFrames = 2;
  std::vector<FrameTimingsPtr> outstanding_frames_;
  bool back_pressure_applied_ = false;
  bool render_continuously_ = false;

  // Lists all Session that have updates to apply, sorted by the earliest
  // requested presentation time of each update.
  std::priority_queue<SessionUpdate, std::vector<SessionUpdate>,
                      UpdatableSessionsComparator>
      updatable_sessions_;

  FrameSchedulerDelegate delegate_;

  fxl::WeakPtrFactory<DefaultFrameScheduler> weak_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(DefaultFrameScheduler);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_DEFAULT_FRAME_SCHEDULER_H_
