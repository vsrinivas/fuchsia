// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_DELEGATING_FRAME_SCHEDULER_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_DELEGATING_FRAME_SCHEDULER_H_

#include <lib/fit/function.h>

#include <vector>

#include "src/lib/fxl/memory/weak_ptr.h"
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
  void SetFrameRenderer(fxl::WeakPtr<FrameRenderer> frame_renderer) override;

  // |FrameScheduler|
  // Calls AddSessionUpdater() immediately if a FrameScheduler has been set; otherwise defers
  // the call until one has been set.
  void AddSessionUpdater(fxl::WeakPtr<SessionUpdater> session_updater) override;

  // |FrameScheduler|
  // Calls SetRenderContinuously() immediately if a FrameScheduler has been set; otherwise defers
  // the call until one has been set.
  void SetRenderContinuously(bool render_continuously) override;

  // |FrameScheduler|
  // Calls ScheduleUpdateForSession() immediately if a FrameScheduler has been set; otherwise defers
  // the call until one has been set.
  void ScheduleUpdateForSession(zx::time presentation_time,
                                scenic_impl::SessionId session_id) override;

  // |FrameScheduler|
  // Calls GetFuturePresentationInfos() immediately if a FrameScheduler has been set; otherwise
  // defers the call until one has been set.
  void GetFuturePresentationInfos(
      zx::duration requested_prediction_span,
      FrameScheduler::GetFuturePresentationInfosCallback callback) override;

  // |FrameScheduler|
  // Calls SetOnFramePresentedCallbackForSession() immediately if a FrameScheduler has been set;
  // otherwise defers the call until one has been set.
  void SetOnFramePresentedCallbackForSession(scenic_impl::SessionId session,
                                             OnFramePresentedCallback callback) override;

  // Sets the frame scheduler, which triggers any pending callbacks. This method cannot be called
  // twice, or called with a null ptr.
  void SetFrameScheduler(std::shared_ptr<FrameScheduler> frame_scheduler);

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
