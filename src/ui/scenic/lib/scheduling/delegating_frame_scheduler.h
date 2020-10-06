// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_DELEGATING_FRAME_SCHEDULER_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_DELEGATING_FRAME_SCHEDULER_H_

#include <lib/fit/function.h>

#include <vector>

#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"

namespace scheduling {

// Wraps a FrameScheduler, but postpones all calls until that FrameScheduler has
// been set. When DelegatingFrameScheduler is created, the wrapped FrameScheduler
// might still be null.
class DelegatingFrameScheduler : public FrameScheduler {
 public:
  DelegatingFrameScheduler() = default;
  DelegatingFrameScheduler(std::shared_ptr<FrameScheduler> frame_scheduler);

  DelegatingFrameScheduler(const DelegatingFrameScheduler&) = delete;
  DelegatingFrameScheduler(DelegatingFrameScheduler&&) = delete;
  DelegatingFrameScheduler& operator=(const DelegatingFrameScheduler&) = delete;
  DelegatingFrameScheduler& operator=(DelegatingFrameScheduler&&) = delete;

  // |FrameScheduler|
  // Calls SetFrameRenderer() immediately if a FrameScheduler has been set; otherwise defers
  // the call until one has been set.
  void SetFrameRenderer(std::weak_ptr<FrameRenderer> frame_renderer) override;

  // |FrameScheduler|
  // Calls AddSessionUpdater() immediately if a FrameScheduler has been set; otherwise defers
  // the call until one has been set.
  void AddSessionUpdater(std::weak_ptr<SessionUpdater> session_updater) override;

  // |FrameScheduler|
  // Calls SetRenderContinuously() immediately if a FrameScheduler has been set; otherwise defers
  // the call until one has been set.
  void SetRenderContinuously(bool render_continuously) override;

  // |FrameScheduler|
  // Calls RegisterPresent() immediately if a FrameScheduler has been set, otherwise defers the call
  // until one has been set. Returns a PresentId unique to the session.
  PresentId RegisterPresent(SessionId session_id,
                            std::variant<OnPresentedCallback, Present2Info> present_information,
                            std::vector<zx::event> release_fences,
                            PresentId present_id = kInvalidPresentId) override;

  // |FrameScheduler|
  // Calls ScheduleUpdateForSession() immediately if a FrameScheduler has been set; otherwise defers
  // the call until one has been set.
  void ScheduleUpdateForSession(zx::time presentation_time, SchedulingIdPair id_pair) override;

  // |FrameScheduler|
  // Calls GetFuturePresentationInfos() immediately if a FrameScheduler has been set; otherwise
  // defers the call until one has been set.
  void GetFuturePresentationInfos(
      zx::duration requested_prediction_span,
      FrameScheduler::GetFuturePresentationInfosCallback callback) override;

  // |FrameScheduler|
  // Calls SetOnFramePresentedCallbackForSession() immediately if a FrameScheduler has been set;
  // otherwise defers the call until one has been set.
  void SetOnFramePresentedCallbackForSession(SessionId session,
                                             OnFramePresentedCallback callback) override;

  // |FrameScheduler|
  // Calls RemoveSession() immediately if a FrameScheduler has been set;
  // otherwise defers the call until one has been set.
  void RemoveSession(SessionId session_id) override;

  // Sets the frame scheduler, which triggers any pending callbacks. This method cannot be called
  // twice, or called with a null pointer.
  void SetFrameScheduler(const std::shared_ptr<FrameScheduler>& frame_scheduler);

 private:
  using OnFrameSchedulerAvailableCallback = fit::function<void(FrameScheduler*)>;

  // Calls |callback| immediately if a FrameScheduler has been set; otherwise defers the call
  // until one has been set.
  void CallWhenFrameSchedulerAvailable(OnFrameSchedulerAvailableCallback callback);

  std::shared_ptr<FrameScheduler> frame_scheduler_;
  std::vector<OnFrameSchedulerAvailableCallback> call_when_frame_scheduler_available_callbacks_;
};

}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_DELEGATING_FRAME_SCHEDULER_H_
