// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_DEFAULT_FRAME_SCHEDULER_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_DEFAULT_FRAME_SCHEDULER_H_

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/time.h>

#include "src/lib/cobalt/cpp/cobalt_logger.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/inspect_deprecated/inspect.h"
#include "src/ui/scenic/lib/scheduling/frame_predictor.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/frame_stats.h"
#include "src/ui/scenic/lib/scheduling/id.h"
#include "src/ui/scenic/lib/scheduling/vsync_timing.h"

namespace scheduling {

// TODOs can be found in the frame scheduler epic: SCN-1202. Any new bugs filed concerning the frame
// scheduler should be added to it as well.
class DefaultFrameScheduler : public FrameScheduler {
 public:
  explicit DefaultFrameScheduler(std::shared_ptr<const VsyncTiming> vsync_timing,
                                 std::unique_ptr<FramePredictor> predictor,
                                 inspect_deprecated::Node inspect_node = inspect_deprecated::Node(),
                                 std::unique_ptr<cobalt::CobaltLogger> cobalt_logger = nullptr);
  ~DefaultFrameScheduler();

  // |FrameScheduler|
  void SetFrameRenderer(fxl::WeakPtr<FrameRenderer> frame_renderer) override;

  // |FrameScheduler|
  void AddSessionUpdater(fxl::WeakPtr<SessionUpdater> session_updater) override;

  // |FrameScheduler|
  //
  // If |render_continuously|, we keep rendering frames regardless of whether they're requested
  // using RequestFrame().
  void SetRenderContinuously(bool render_continuously) override;

  // |FrameScheduler|
  //
  // Tell the FrameScheduler to schedule a frame. This is also used for updates triggered by
  // something other than a Session update i.e. an ImagePipe with a new Image to present.
  void ScheduleUpdateForSession(zx::time presentation_time, SessionId session) override;

  // Sets the |fuchsia::ui::scenic::Session::OnFramePresented| event handler. This should only be
  // called once per session.
  void SetOnFramePresentedCallbackForSession(SessionId session,
                                             OnFramePresentedCallback callback) override;

  // |FrameScheduler|
  std::vector<fuchsia::scenic::scheduling::PresentationInfo> GetFuturePresentationInfos(
      zx::duration requested_prediction_span) override;

  constexpr static zx::duration kInitialRenderDuration = zx::msec(5);
  constexpr static zx::duration kInitialUpdateDuration = zx::msec(1);

  // Public for testing.
  constexpr static size_t kMaxOutstandingFrames = 2;

  // Helper class that manages:
  // - registration of SessionUpdaters
  // - tracking callbacks that need to be invoked.
  class UpdateManager {
   public:
    UpdateManager() = default;

    // Add |session_updater| to the list of updaters on which |UpdateSessions()| and
    // |PrepareFrame()| will be invoked.
    void AddSessionUpdater(fxl::WeakPtr<SessionUpdater> session_updater);

    // Schedules an update for the specified session.  All updaters registered by
    // |AddSessionUpdater()| are notified when |ApplyUpdates()| is called with an equal or later
    // presentation time.
    void ScheduleUpdate(zx::time presentation_time, SessionId session);

    // Returned by |ApplyUpdates()|; used by a |FrameScheduler| to decide whether to render a frame
    // and/or schedule another frame to be rendered.
    struct ApplyUpdatesResult {
      bool needs_render;
      bool needs_reschedule;
    };
    // Calls |SessionUpdater::UpdateSessions()| on all updaters, and uses the returned
    // |SessionUpdater::UpdateResults| to generate the returned |ApplyUpdatesResult|.
    ApplyUpdatesResult ApplyUpdates(zx::time target_presentation_time, zx::time latched_time,
                                    zx::duration vsync_interval, uint64_t frame_number);

    // Return true if there are any scheduled session updates that have not yet been applied.
    bool HasUpdatableSessions() const { return !updatable_sessions_.empty(); }

    zx::time EarliestRequestedPresentationTime() {
      FXL_DCHECK(HasUpdatableSessions());
      return updatable_sessions_.top().requested_presentation_time;
    }

    // Creates a ratchet point for the updater. All present calls that were updated before this
    // point will be signaled with the next call to |SignalPresentCallbacks()|.
    void RatchetPresentCallbacks(zx::time presentation_time, uint64_t frame_number);

    // Signal that all updates before the last ratchet point have been presented.  The signaled
    // callbacks are every successful present between the last time |SignalPresentCallbacks()| was
    // called and the most recent call to |RatchetPresentCallbacks()|.
    void SignalPresentCallbacks(fuchsia::images::PresentationInfo info);

    // Sets the |fuchsia::ui::scenic::Session::OnFramePresented| event handler. This should only be
    // called once per session.
    void SetOnFramePresentedCallbackForSession(SessionId session,
                                               OnFramePresentedCallback callback);

   private:
    std::vector<fxl::WeakPtr<SessionUpdater>> session_updaters_;

    // Sessions that have updates to apply, sorted by requested presentation time from earliest to
    // latest.
    struct SessionUpdate {
      SessionId session_id;
      zx::time requested_presentation_time;

      bool operator>(const SessionUpdate& rhs) const {
        return requested_presentation_time > rhs.requested_presentation_time;
      }
    };
    std::priority_queue<SessionUpdate, std::vector<SessionUpdate>, std::greater<SessionUpdate>>
        updatable_sessions_;

    std::deque<OnPresentedCallback> present1_callbacks_this_frame_;
    std::deque<OnPresentedCallback> pending_present1_callbacks_;

    std::deque<scenic_impl::Present2Info> present2_infos_this_frame_;
    std::multimap<SessionId, scenic_impl::Present2Info> pending_present2_infos_;

    std::map<SessionId, OnFramePresentedCallback> present2_callback_map_;
  };

 protected:
  // |FrameScheduler|
  void OnFramePresented(const FrameTimings& timings) override;

  // |FrameScheduler|
  void OnFrameRendered(const FrameTimings& timings) override;

 private:
  // Requests a new frame to be drawn, which schedules the next wake up time for rendering. If we've
  // already scheduled a wake up time, it checks if it needs rescheduling and deals with it
  // appropriately.
  void RequestFrame();

  // Update the global scene and then draw it... maybe.  There are multiple reasons why this might
  // not happen.  For example, the swapchain might apply back-pressure if we can't hit our target
  // frame rate. Or, the frame before this one has yet to finish rendering. Etc.
  void MaybeRenderFrame(async_dispatcher_t*, async::TaskBase*, zx_status_t);

  // Computes the target presentation time for the requested presentation time, and a wake-up time
  // that is early enough to start rendering in order to hit the target presentation time. These
  // times are guaranteed to be in the future.
  std::pair<zx::time, zx::time> ComputePresentationAndWakeupTimesForTargetTime(
      zx::time requested_presentation_time) const;

  // Executes updates that are scheduled up to and including a given presentation time.
  UpdateManager::ApplyUpdatesResult ApplyUpdates(zx::time target_presentation_time,
                                                 zx::time latched_time);

  // References.
  async_dispatcher_t* const dispatcher_;
  const std::shared_ptr<const VsyncTiming> vsync_timing_;

  fxl::WeakPtr<FrameRenderer> frame_renderer_;

  // State.
  uint64_t frame_number_ = 0;
  std::vector<std::unique_ptr<FrameTimings>> outstanding_frames_;
  bool render_continuously_ = false;
  bool currently_rendering_ = false;
  bool render_pending_ = false;
  zx::time wakeup_time_;
  zx::time next_presentation_time_;
  UpdateManager update_manager_;
  std::unique_ptr<FramePredictor> frame_predictor_;

  // The async task that wakes up to start rendering.
  async::TaskMethod<DefaultFrameScheduler, &DefaultFrameScheduler::MaybeRenderFrame>
      frame_render_task_{this};

  inspect_deprecated::Node inspect_node_;
  inspect_deprecated::UIntMetric inspect_frame_number_;
  inspect_deprecated::UIntMetric inspect_last_successful_update_start_time_;
  inspect_deprecated::UIntMetric inspect_last_successful_render_start_time_;

  FrameStats stats_;

  fxl::WeakPtrFactory<DefaultFrameScheduler> weak_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(DefaultFrameScheduler);
};

}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_DEFAULT_FRAME_SCHEDULER_H_
