// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_DEFAULT_FRAME_SCHEDULER_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_DEFAULT_FRAME_SCHEDULER_H_

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/time.h>

#include <list>

#include "lib/inspect/cpp/inspect.h"
#include "src/lib/cobalt/cpp/cobalt_logger.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/scheduling/frame_predictor.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/frame_stats.h"
#include "src/ui/scenic/lib/scheduling/id.h"
#include "src/ui/scenic/lib/scheduling/vsync_timing.h"
#include "src/ui/scenic/lib/utils/sequential_fence_signaller.h"

namespace scheduling {

// TODOs can be found in the frame scheduler epic: fxbug.dev/24406. Any new bugs filed concerning
// the frame scheduler should be added to it as well.
class DefaultFrameScheduler final : public FrameScheduler {
 public:
  explicit DefaultFrameScheduler(std::shared_ptr<const VsyncTiming> vsync_timing,
                                 std::unique_ptr<FramePredictor> predictor,
                                 inspect::Node inspect_node = inspect::Node(),
                                 std::shared_ptr<cobalt::CobaltLogger> cobalt_logger = nullptr);
  ~DefaultFrameScheduler();

  // |FrameScheduler|
  void SetFrameRenderer(std::weak_ptr<FrameRenderer> frame_renderer) override;

  // |FrameScheduler|
  void AddSessionUpdater(std::weak_ptr<SessionUpdater> session_updater) override;

  // |FrameScheduler|
  //
  // If |render_continuously|, we keep rendering frames regardless of whether they're requested
  // using RequestFrame().
  void SetRenderContinuously(bool render_continuously) override;

  // |FrameScheduler|
  PresentId RegisterPresent(SessionId session_id,
                            std::variant<OnPresentedCallback, Present2Info> present_information,
                            std::vector<zx::event> release_fences,
                            PresentId present_id = 0) override;

  // |FrameScheduler|
  //
  // Tell the FrameScheduler to schedule a frame. This is also used for updates triggered by
  // something other than a Session update i.e. an ImagePipe with a new Image to present.
  void ScheduleUpdateForSession(zx::time requested_presentation_time,
                                SchedulingIdPair id_pair) override;

  // |FrameScheduler|
  //
  // Sets the |fuchsia::ui::scenic::Session::OnFramePresented| event handler. This should only be
  // called once per session.
  void SetOnFramePresentedCallbackForSession(
      SessionId session, OnFramePresentedCallback frame_presented_callback) override;

  // |FrameScheduler|
  void GetFuturePresentationInfos(
      zx::duration requested_prediction_span,
      FrameScheduler::GetFuturePresentationInfosCallback presentation_infos_callback) override;

  // |FrameScheduler|
  //
  // Remove all state associated with a given session_id.
  void RemoveSession(SessionId session_id) override;

  constexpr static zx::duration kMinPredictedFrameDuration = zx::msec(0);
  constexpr static zx::duration kInitialRenderDuration = zx::msec(5);
  constexpr static zx::duration kInitialUpdateDuration = zx::msec(1);

  // Public for testing.
  constexpr static size_t kMaxOutstandingFrames = 2;

 protected:
  void OnFramePresented(const FrameTimings& timings);

  void OnFrameRendered(const FrameTimings& timings);

 private:
  // Requests a new frame to be drawn, which schedules the next wake up time for rendering. If we've
  // already scheduled a wake up time, it checks if it needs rescheduling and deals with it
  // appropriately.
  void RequestFrame(zx::time requested_presentation_time);

  // Check if there are pending updates, and if there are then find the next lowest requested
  // presentation time and uses it to request another frame.
  void HandleNextFrameRequest();

  // Update the global scene and then draw it... maybe.  There are multiple reasons why this might
  // not happen.  For example, the swapchain might apply back-pressure if we can't hit our target
  // frame rate. Or, the frame before this one has yet to finish rendering. Etc.
  void MaybeRenderFrame(async_dispatcher_t*, async::TaskBase*, zx_status_t);

  // Computes the target presentation time for the requested presentation time, and a wake-up time
  // that is early enough to start rendering in order to hit the target presentation time. These
  // times are guaranteed to be in the future.
  std::pair<zx::time, zx::time> ComputePresentationAndWakeupTimesForTargetTime(
      zx::time requested_presentation_time) const;

  // Adds pending SessionUpdaters to the active list and clears out stale ones.
  void RefreshSessionUpdaters();

  // Executes updates that are scheduled up to and including a given presentation time.
  bool ApplyUpdates(zx::time target_presentation_time, zx::time latched_time,
                    uint64_t frame_number);

  // Return true if there are any scheduled session updates that have not yet been applied.
  bool HaveUpdatableSessions() const { return !pending_present_requests_.empty(); }

  // Signal all callbacks prepared on frames up to |frame_number|.
  void SignalPresentCallbacksUpTo(uint64_t frame_number,
                                  fuchsia::images::PresentationInfo presentation_info);

  // Signal all Present1 callbacks for |id_pair.session| up to |id_pair.present_id|.
  void SignalPresent1CallbacksUpTo(SchedulingIdPair id_pair,
                                   fuchsia::images::PresentationInfo presentation_info);

  // Signal all Present2 callbacks for |id_pair.session| up to |id_pair.present_id|.
  void SignalPresent2CallbackForInfosUpTo(SchedulingIdPair id_pair, zx::time presented_time);

  // Get map of latch times for each present up to |id_pair.present_id| for |id_pair.session_id|.
  std::map<PresentId, zx::time> ExtractLatchTimestampsUpTo(SchedulingIdPair id_pair);

  // Set all unset latched times for each registered present of |session_id|, up to and including
  // |present_id|.
  void SetLatchedTimeForPresentsUpTo(SchedulingIdPair id_pair, zx::time latched_time);

  // Set all unset latched times for each Present2Info of |session_id|, up to and including
  // |present_id|.
  void SetLatchedTimeForPresent2Infos(SchedulingIdPair id_pair, zx::time latched_time);

  // Move all fences before |present_id| to the signaller to be signalled at next OnFrameRendered.
  void MoveReleaseFencesToSignaller(SchedulingIdPair id_pair, uint64_t frame_number);

  // Extracts all presents that should be updated this frame and returns them as a map of SessionIds
  // to the last PresentId that should be updated for that session.
  std::unordered_map<SessionId, PresentId> CollectUpdatesForThisFrame(
      zx::time target_presentation_time);

  // Prepares all per-present data for later OnFrameRendered and OnFramePresented events.
  void PrepareUpdates(const std::unordered_map<SessionId, PresentId>& updates,
                      zx::time latched_time, uint64_t frame_number);

  // Cycles through SessionUpdaters, applies updates to each and coalesces their responses.
  SessionUpdater::UpdateResults ApplyUpdatesToEachUpdater(
      const std::unordered_map<SessionId, PresentId>& sessions_to_update, uint64_t frame_number);

  // Removes all references to each session passed in.
  void RemoveFailedSessions(const std::unordered_set<SessionId>& sessions_with_failed_updates);

  // Map of all pending Present calls ordered by SessionId and then PresentId. Maps to requested
  // presentation time for each present.
  std::map<SchedulingIdPair, zx::time> pending_present_requests_;

  // TODO(fxbug.dev/47308): A lot of logic is temporarily duplicated while clients are being converted over.
  // When both session and and image pipes have been converted to handling their own callbacks,
  // delete unnecessary tracking state.
  struct FrameUpdate {
    uint64_t frame_number;
    std::unordered_map<SessionId, PresentId> updated_sessions;
    zx::time latched_time;
  };
  // Queue of session updates mapped to frame numbers. Used when triggering callbacks in
  // OnFramePresented.
  std::queue<FrameUpdate> latched_updates_;

  // All currently tracked presents and their associated latched_times.
  std::map<SchedulingIdPair, /*latched_time*/ std::optional<zx::time>> presents_;

  // Ordered maps of per-present data ordered by SessionId and then PresentId.
  // Per-present callbacks for Present1 and ImagePipe clients.
  std::map<SchedulingIdPair, OnPresentedCallback> present1_callbacks_;
  // Per-present info for Present2 clients, to be coalesced before used in callback.
  std::map<SchedulingIdPair, Present2Info> present2_infos_;
  // Per-present release fences. To be released as each subsequent present for each session is
  // rendered.
  std::map<SchedulingIdPair, std::vector<zx::event>> release_fences_;

  // Map of registered callbacks for Present2 sessions.
  std::unordered_map<SessionId, OnFramePresentedCallback> present2_callback_map_;

  utils::SequentialFenceSignaller release_fence_signaller_;

  // Set of SessionUpdaters to update. Stored as a weak_ptr: when the updaters become
  // invalid, the weak_ptr is removed from this list.
  std::vector<std::weak_ptr<SessionUpdater>> session_updaters_;

  // Stores SessionUpdaters we added to the DefaultFrameScheduler. Upon
  // ApplyUpdates() is called, these SessionUpdaters will be moved to
  // the |session_updaters_| vector.
  // Exists to protect against when new session updaters are added mid-update.
  std::list<std::weak_ptr<SessionUpdater>> new_session_updaters_;

  // References.
  async_dispatcher_t* const dispatcher_;
  const std::shared_ptr<const VsyncTiming> vsync_timing_;

  std::weak_ptr<FrameRenderer> frame_renderer_;

  // State.
  uint64_t frame_number_ = 0;
  std::vector<std::unique_ptr<FrameTimings>> outstanding_frames_;
  bool render_continuously_ = false;
  bool currently_rendering_ = false;
  bool render_pending_ = false;
  zx::time wakeup_time_;
  zx::time next_target_presentation_time_;
  std::unique_ptr<FramePredictor> frame_predictor_;

  // The async task that wakes up to start rendering.
  async::TaskMethod<DefaultFrameScheduler, &DefaultFrameScheduler::MaybeRenderFrame>
      frame_render_task_{this};

  inspect::Node inspect_node_;
  inspect::UintProperty inspect_frame_number_;
  inspect::UintProperty inspect_last_successful_update_start_time_;
  inspect::UintProperty inspect_last_successful_render_start_time_;

  FrameStats stats_;

  // For tracing.
  uint64_t frame_render_trace_id_ = 0;
  // Maps wakeup time to trace IDs, to properly match up renders for frames >1 vsyncs away.
  uint64_t request_to_render_count_ = 0;
  std::multimap<zx::time, uint64_t> render_wakeup_map_ = {};

  fxl::WeakPtrFactory<DefaultFrameScheduler> weak_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(DefaultFrameScheduler);
};

}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_DEFAULT_FRAME_SCHEDULER_H_
