// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_TESTS_MOCKS_FRAME_SCHEDULER_MOCKS_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_TESTS_MOCKS_FRAME_SCHEDULER_MOCKS_H_

#include <set>
#include <unordered_map>
#include <variant>

#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/frame_timings.h"
#include "src/ui/scenic/lib/scheduling/vsync_timing.h"

namespace scheduling {
namespace test {

class MockFrameScheduler : public FrameScheduler {
 public:
  MockFrameScheduler() = default;

  // |FrameScheduler|
  void SetFrameRenderer(fxl::WeakPtr<FrameRenderer> frame_renderer) override {}
  // |FrameScheduler|
  void AddSessionUpdater(fxl::WeakPtr<SessionUpdater> session_updater) override {}
  // |FrameScheduler|
  void SetRenderContinuously(bool render_continuously) override;

  PresentId RegisterPresent(SessionId session_id,
                            std::variant<OnPresentedCallback, Present2Info> present_information,
                            std::vector<zx::event> release_fences, PresentId present_id) override;

  // |FrameScheduler|
  void SetOnUpdateFailedCallbackForSession(
      SessionId session, OnSessionUpdateFailedCallback update_failed_callback) override {}

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
  void RemoveSession(SessionId session_id) override {}

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

  void set_next_present_id(PresentId present_id) { next_present_id_ = present_id; }

 private:
  // Testing only. Mock method callbacks.
  OnSetRenderContinuouslyCallback set_render_continuously_callback_;
  OnScheduleUpdateForSessionCallback schedule_update_for_session_callback_;
  OnGetFuturePresentationInfosCallback get_future_presentation_infos_callback_;
  OnSetOnFramePresentedCallbackForSessionCallback
      set_on_frame_presented_callback_for_session_callback_;
  RegisterPresentCallback register_present_callback_;

  PresentId next_present_id_;
};

class MockSessionUpdater : public SessionUpdater {
 public:
  MockSessionUpdater() : weak_factory_(this) {}

  // |SessionUpdater|
  SessionUpdater::UpdateResults UpdateSessions(
      const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
      uint64_t trace_id = 0) override {
    ++update_sessions_call_count_;
    last_sessions_to_update_ = sessions_to_update;
    return update_sessions_return_value_;
  }

  // |SessionUpdater|
  void PrepareFrame(uint64_t frame_number) override { ++prepare_frame_call_count_; }

  void SetUpdateSessionsReturnValue(SessionUpdater::UpdateResults new_value) {
    update_sessions_return_value_ = new_value;
  }

  fxl::WeakPtr<MockSessionUpdater> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  uint64_t update_sessions_call_count() { return update_sessions_call_count_; }
  uint64_t prepare_frame_call_count() { return prepare_frame_call_count_; }
  std::unordered_map<scheduling::SessionId, scheduling::PresentId> last_sessions_to_update() {
    return last_sessions_to_update_;
  };

 private:
  SessionUpdater::UpdateResults update_sessions_return_value_;

  uint64_t prepare_frame_call_count_ = 0;
  uint64_t update_sessions_call_count_ = 0;
  std::unordered_map<scheduling::SessionId, scheduling::PresentId> last_sessions_to_update_;

  fxl::WeakPtrFactory<MockSessionUpdater> weak_factory_;  // must be last
};

class MockFrameRenderer : public FrameRenderer {
 public:
  MockFrameRenderer() : weak_factory_(this) {}

  // |FrameRenderer|
  RenderFrameResult RenderFrame(fxl::WeakPtr<FrameTimings> frame_timings,
                                zx::time presentation_time) override;

  // Need to call this in order to trigger the OnFramePresented() callback in
  // FrameScheduler, but is not valid to do until after RenderFrame has returned
  // to FrameScheduler. Hence this separate method.
  void EndFrame(size_t frame_index, zx::time time_done);

  // Signal frame |frame_index| that it has been rendered.
  void SignalFrameRendered(uint64_t frame_number, zx::time time_done);

  // Signal frame |frame_index| that the CPU portion of rendering is done.
  void SignalFrameCpuRendered(uint64_t frame_number, zx::time time_done);

  // Signal frame |frame_index| that it has been presented.
  void SignalFramePresented(uint64_t frame_number, zx::time time_done);

  // Signal frame |frame_index| that it has been dropped.
  void SignalFrameDropped(uint64_t frame_number);

  // Manually set value returned from RenderFrame.
  void SetRenderFrameReturnValue(RenderFrameResult new_value) {
    render_frame_return_value_ = new_value;
  }
  uint32_t render_frame_call_count() { return render_frame_call_count_; }
  size_t pending_frames() { return frames_.size(); }

  fxl::WeakPtr<MockFrameRenderer> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  void CleanUpFrame(uint64_t frame_number);

  RenderFrameResult render_frame_return_value_ = RenderFrameResult::kRenderSuccess;
  uint32_t render_frame_call_count_ = 0;

  struct Timings {
    fxl::WeakPtr<FrameTimings> frame_timings;
    size_t swapchain_index = -1;
    uint32_t frame_rendered = false;
    uint32_t frame_cpu_rendered = false;
    uint32_t frame_presented = false;
  };
  std::unordered_map<uint64_t, Timings> frames_;

  uint64_t last_frame_number_ = -1;

  fxl::WeakPtrFactory<MockFrameRenderer> weak_factory_;  // must be last
};

}  // namespace test
}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_TESTS_MOCKS_FRAME_SCHEDULER_MOCKS_H_
