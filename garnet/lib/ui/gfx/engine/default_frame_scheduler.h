// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_DEFAULT_FRAME_SCHEDULER_H_
#define GARNET_LIB_UI_GFX_ENGINE_DEFAULT_FRAME_SCHEDULER_H_

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/inspect/inspect.h>
#include <lib/zx/time.h>

#include <queue>

#include "garnet/lib/ui/gfx/engine/frame_scheduler.h"
#include "garnet/lib/ui/gfx/id.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace scenic_impl {
namespace gfx {

using SessionUpdate = SessionUpdater::SessionUpdate;
class Display;

// TODOs can be found in the frame scheduler epic: SCN-1202. Any new bugs filed
// concerning the frame scheduler should be added to it as well.
class DefaultFrameScheduler : public FrameScheduler {
 public:
  explicit DefaultFrameScheduler(
      const Display* display,
      inspect::Object inspect_object = inspect::Object());
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
  void ScheduleUpdateForSession(zx_time_t presentation_time,
                                scenic_impl::SessionId session) override;

  // Public for testing.
  constexpr static size_t kMaxOutstandingFrames = 2;

 protected:
  // |FrameScheduler|
  void OnFramePresented(const FrameTimings& timings) override;

  // |FrameScheduler|
  void OnFrameRendered(const FrameTimings& timings) override;

 private:
  // Requests a new frame to be drawn, which schedules the next wake up time for
  // rendering. If we've already scheduled a wake up time, it checks if it needs
  // rescheduling and deals with it appropriately.
  void RequestFrame();

  // Update the global scene and then draw it... maybe.  There are multiple
  // reasons why this might not happen.  For example, the swapchain might apply
  // back-pressure if we can't hit our target frame rate. Or, the frame before
  // this one has yet to finish rendering. Etc.
  void MaybeRenderFrame(async_dispatcher_t*, async::TaskBase*, zx_status_t);

  // Computes the target presentation time for the requested presentation time,
  // and a wake-up time that is early enough to start rendering in order to hit
  // the target presentation time. These times are guaranteed to be in the
  // future.
  std::pair<zx_time_t, zx_time_t>
  ComputePresentationAndWakeupTimesForTargetTime(
      zx_time_t requested_presentation_time) const;

  // Return the predicted amount of time required to render a frame.
  zx_time_t PredictRequiredFrameRenderTime() const;

  // Executes updates that are scheduled up to and including a given
  // presentation time. Returns true if rendering is needed.
  bool ApplyScheduledSessionUpdates(zx_time_t presentation_time);

  // References.
  async_dispatcher_t* const dispatcher_;
  const Display* const display_;
  FrameSchedulerDelegate delegate_;

  // State.
  uint64_t frame_number_ = 0;
  std::vector<FrameTimingsPtr> outstanding_frames_;
  bool render_continuously_ = false;
  bool currently_rendering_ = false;
  bool render_pending_ = false;
  zx_time_t wakeup_time_;
  zx_time_t next_presentation_time_;

  // The async task that wakes up to start rendering.
  async::TaskMethod<DefaultFrameScheduler,
                    &DefaultFrameScheduler::MaybeRenderFrame>
      frame_render_task_{this};

  // Sessions that have updates to apply, sorted by requested presentation time
  // from earliest to latest.
  std::priority_queue<SessionUpdate, std::vector<SessionUpdate>,
                      std::greater<SessionUpdate>>
      updatable_sessions_;

  inspect::Object inspect_object_;
  inspect::UIntMetric inspect_frame_number_;

  fxl::WeakPtrFactory<DefaultFrameScheduler> weak_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(DefaultFrameScheduler);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_DEFAULT_FRAME_SCHEDULER_H_
