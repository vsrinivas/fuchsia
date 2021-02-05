// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_TESTS_MOCKS_FRAME_SCHEDULER_MOCKS_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_TESTS_MOCKS_FRAME_SCHEDULER_MOCKS_H_

#include <set>
#include <unordered_map>
#include <variant>

#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/vsync_timing.h"

namespace scheduling {
namespace test {

class MockFrameScheduler : public FrameScheduler {
 public:
  MockFrameScheduler() = default;

  // |FrameScheduler|
  void SetFrameRenderer(std::weak_ptr<FrameRenderer> frame_renderer) override {}
  // |FrameScheduler|
  void AddSessionUpdater(std::weak_ptr<SessionUpdater> session_updater) override {}
  // |FrameScheduler|
  void SetRenderContinuously(bool render_continuously) override;

  PresentId RegisterPresent(SessionId session_id,
                            std::variant<OnPresentedCallback, Present2Info> present_information,
                            std::vector<zx::event> release_fences, PresentId present_id) override;

  // |FrameScheduler|
  void ScheduleUpdateForSession(zx::time presentation_time, SchedulingIdPair id_pair) override;

  // |FrameScheduler|
  void GetFuturePresentationInfos(
      zx::duration requested_prediction_span,
      FrameScheduler::GetFuturePresentationInfosCallback presentation_infos_callback) override;

  // |FrameScheduler|
  void SetOnFramePresentedCallbackForSession(
      SessionId session, OnFramePresentedCallback frame_presented_callback) override;

  // |FrameScheduler|
  void RemoveSession(SessionId session_id) override;

  // Testing only. Used for mock method callbacks.
  using OnSetRenderContinuouslyCallback = std::function<void(bool)>;
  using OnScheduleUpdateForSessionCallback = std::function<void(zx::time, SchedulingIdPair)>;
  using OnGetFuturePresentationInfosCallback =
      std::function<std::vector<fuchsia::scenic::scheduling::PresentationInfo>(
          zx::duration requested_prediction_span)>;
  using OnSetOnFramePresentedCallbackForSessionCallback =
      std::function<void(SessionId session, OnFramePresentedCallback callback)>;
  using RegisterPresentCallback = std::function<void(
      SessionId session_id, std::variant<OnPresentedCallback, Present2Info> present_information,
      std::vector<zx::event> release_fences, PresentId present_id)>;
  using RemoveSessionCallback = std::function<void(SessionId session_id)>;

  // Testing only. Sets mock method callback.
  void set_set_render_continuously_callback(OnSetRenderContinuouslyCallback callback) {
    set_render_continuously_callback_ = callback;
  }

  // Testing only. Sets mock method callback.
  void set_schedule_update_for_session_callback(OnScheduleUpdateForSessionCallback callback) {
    schedule_update_for_session_callback_ = callback;
  }

  // Testing only. Sets mock method callback.
  void set_get_future_presentation_infos_callback(OnGetFuturePresentationInfosCallback callback) {
    get_future_presentation_infos_callback_ = callback;
  }

  // Testing only. Sets mock method callback.
  void set_set_on_frame_presented_callback_for_session_callback(
      OnSetOnFramePresentedCallbackForSessionCallback callback) {
    set_on_frame_presented_callback_for_session_callback_ = callback;
  }

  // Testing only. Sets mock method callback.
  void set_register_present_callback(RegisterPresentCallback callback) {
    register_present_callback_ = callback;
  }

  // Testing only. Sets mock method callback.
  void set_remove_session_callback(RemoveSessionCallback callback) {
    remove_session_callback_ = callback;
  }

  void set_next_present_id(PresentId present_id) { next_present_id_ = present_id; }

 private:
  // Testing only. Mock method callbacks.
  OnSetRenderContinuouslyCallback set_render_continuously_callback_;
  OnScheduleUpdateForSessionCallback schedule_update_for_session_callback_;
  OnGetFuturePresentationInfosCallback get_future_presentation_infos_callback_;
  OnSetOnFramePresentedCallbackForSessionCallback
      set_on_frame_presented_callback_for_session_callback_;
  RegisterPresentCallback register_present_callback_;
  RemoveSessionCallback remove_session_callback_;

  PresentId next_present_id_;
};

class MockSessionUpdater : public SessionUpdater {
 public:
  MockSessionUpdater() {}

  // |SessionUpdater|
  SessionUpdater::UpdateResults UpdateSessions(
      const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
      uint64_t trace_id = 0) override {
    ++update_sessions_call_count_;
    last_sessions_to_update_ = sessions_to_update;
    return update_sessions_return_value_;
  }

  // |SessionUpdater|
  void OnFramePresented(
      const std::unordered_map<scheduling::SessionId,
                               std::map<scheduling::PresentId, /*latched_time*/ zx::time>>&
          latched_times,
      scheduling::PresentTimestamps present_times) override {
    last_latched_times_ = latched_times;
    last_presented_time_ = present_times.presented_time;
  }

  void SetUpdateSessionsReturnValue(SessionUpdater::UpdateResults new_value) {
    update_sessions_return_value_ = new_value;
  }

  uint64_t update_sessions_call_count() { return update_sessions_call_count_; }
  std::unordered_map<scheduling::SessionId, scheduling::PresentId> last_sessions_to_update() {
    return last_sessions_to_update_;
  };

  std::unordered_map<SessionId, std::map<PresentId, zx::time>> last_latched_times() const {
    return last_latched_times_;
  }

  zx::time last_presented_time() const { return last_presented_time_; }

 private:
  SessionUpdater::UpdateResults update_sessions_return_value_;

  uint64_t update_sessions_call_count_ = 0;
  std::unordered_map<scheduling::SessionId, scheduling::PresentId> last_sessions_to_update_;

  std::unordered_map<scheduling::SessionId,
                     std::map<scheduling::PresentId, /*latched_time*/ zx::time>>
      last_latched_times_;
  zx::time last_presented_time_;
};

class MockFrameRenderer : public FrameRenderer {
 public:
  MockFrameRenderer() {}

  // |FrameRenderer|
  void RenderFrame(FramePresentedCallback callback, uint64_t frame_number,
                   zx::time presentation_time) override;

  // |FrameRenderer|
  void SignalFencesWhenPreviousRendersAreDone(std::vector<zx::event> events) override;

  // Signal the next pending frame has been presented (with default timestamp data)
  void EndFrame();

  // Signal the next pending frame has been presented (with custom timestamp data)
  void EndFrame(const Timestamps& timestamps);

  // Signal the next pending frame has been dropped.
  void DropFrame();

  size_t GetNumPendingFrames() { return frames_.size(); }

 private:
  void CleanUpFrame(uint64_t frame_number);

  struct PendingFrame {
    zx::time start;
    FramePresentedCallback callback;
    std::vector<zx::event> fences;
  };
  std::deque<PendingFrame> frames_;
  std::vector<zx::event> pending_fences_;

  uint64_t last_frame_number_ = 0;
};

}  // namespace test
}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_TESTS_MOCKS_FRAME_SCHEDULER_MOCKS_H_
