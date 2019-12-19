// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/delegating_frame_scheduler.h"

#include "src/lib/fxl/logging.h"

namespace scheduling {

DelegatingFrameScheduler::DelegatingFrameScheduler(
    std::shared_ptr<FrameScheduler> frame_scheduler) {
  if (frame_scheduler) {
    SetFrameScheduler(frame_scheduler);
  }
}

void DelegatingFrameScheduler::SetFrameRenderer(fxl::WeakPtr<FrameRenderer> frame_renderer) {
  CallWhenFrameSchedulerAvailable(
      [frame_renderer = frame_renderer](FrameScheduler* frame_scheduler) {
        frame_scheduler->SetFrameRenderer(frame_renderer);
      });
};

void DelegatingFrameScheduler::AddSessionUpdater(fxl::WeakPtr<SessionUpdater> session_updater) {
  CallWhenFrameSchedulerAvailable(
      [session_updater = session_updater](FrameScheduler* frame_scheduler) {
        frame_scheduler->AddSessionUpdater(session_updater);
      });
};

void DelegatingFrameScheduler::SetRenderContinuously(bool render_continuously) {
  CallWhenFrameSchedulerAvailable([render_continuously](FrameScheduler* frame_scheduler) {
    frame_scheduler->SetRenderContinuously(render_continuously);
  });
}

void DelegatingFrameScheduler::SetOnUpdateFailedCallbackForSession(
    SessionId session_id, FrameScheduler::OnSessionUpdateFailedCallback update_failed_callback) {
  CallWhenFrameSchedulerAvailable([session_id, callback = std::move(update_failed_callback)](
                                      FrameScheduler* frame_scheduler) mutable {
    frame_scheduler->SetOnUpdateFailedCallbackForSession(session_id, std::move(callback));
  });
}

void DelegatingFrameScheduler::ScheduleUpdateForSession(zx::time presentation_time,
                                                        SessionId session_id) {
  CallWhenFrameSchedulerAvailable([presentation_time, session_id](FrameScheduler* frame_scheduler) {
    frame_scheduler->ScheduleUpdateForSession(presentation_time, session_id);
  });
}

void DelegatingFrameScheduler::GetFuturePresentationInfos(
    zx::duration requested_prediction_span, GetFuturePresentationInfosCallback callback) {
  FXL_DCHECK(callback);
  CallWhenFrameSchedulerAvailable([requested_prediction_span, callback = std::move(callback)](
                                      FrameScheduler* frame_scheduler) mutable {
    frame_scheduler->GetFuturePresentationInfos(requested_prediction_span, std::move(callback));
  });
}

void DelegatingFrameScheduler::SetOnFramePresentedCallbackForSession(
    SessionId session, OnFramePresentedCallback callback) {
  if (callback) {
    CallWhenFrameSchedulerAvailable(
        [session, callback = std::move(callback)](FrameScheduler* frame_scheduler) mutable {
          frame_scheduler->SetOnFramePresentedCallbackForSession(session, std::move(callback));
        });
  }
}

void DelegatingFrameScheduler::ClearCallbacksForSession(SessionId session_id) {
  CallWhenFrameSchedulerAvailable([session_id](FrameScheduler* frame_scheduler) {
    frame_scheduler->ClearCallbacksForSession(session_id);
  });
}

void DelegatingFrameScheduler::CallWhenFrameSchedulerAvailable(
    OnFrameSchedulerAvailableCallback callback) {
  if (frame_scheduler_) {
    callback(frame_scheduler_.get());
  } else {
    call_when_frame_scheduler_available_callbacks_.push_back(std::move(callback));
  }
}

void DelegatingFrameScheduler::SetFrameScheduler(std::shared_ptr<FrameScheduler> frame_scheduler) {
  if (frame_scheduler_) {
    FXL_LOG(ERROR) << "DelegatingFrameScheduler can only be set once.";
    return;
  }
  if (!frame_scheduler) {
    FXL_LOG(ERROR) << "DelegatingFrameScheduler cannot be set to a null value.";
    return;
  }
  frame_scheduler_ = frame_scheduler;
  for (auto& callback : call_when_frame_scheduler_available_callbacks_) {
    callback(frame_scheduler_.get());
  }
  call_when_frame_scheduler_available_callbacks_.clear();
}

}  // namespace scheduling
