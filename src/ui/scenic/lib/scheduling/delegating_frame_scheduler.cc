// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/delegating_frame_scheduler.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/scenic/lib/scheduling/id.h"

namespace scheduling {

DelegatingFrameScheduler::DelegatingFrameScheduler(
    std::shared_ptr<FrameScheduler> frame_scheduler) {
  if (frame_scheduler) {
    SetFrameScheduler(frame_scheduler);
  }
}

void DelegatingFrameScheduler::SetFrameRenderer(std::weak_ptr<FrameRenderer> frame_renderer) {
  CallWhenFrameSchedulerAvailable(
      [frame_renderer = std::move(frame_renderer)](FrameScheduler* frame_scheduler) {
        frame_scheduler->SetFrameRenderer(std::move(frame_renderer));
      });
};

void DelegatingFrameScheduler::AddSessionUpdater(std::weak_ptr<SessionUpdater> session_updater) {
  CallWhenFrameSchedulerAvailable(
      [session_updater = std::move(session_updater)](FrameScheduler* frame_scheduler) {
        frame_scheduler->AddSessionUpdater(std::move(session_updater));
      });
};

void DelegatingFrameScheduler::SetRenderContinuously(bool render_continuously) {
  CallWhenFrameSchedulerAvailable([render_continuously](FrameScheduler* frame_scheduler) {
    frame_scheduler->SetRenderContinuously(render_continuously);
  });
}

PresentId DelegatingFrameScheduler::RegisterPresent(
    SessionId session_id, std::variant<OnPresentedCallback, Present2Info> present_information,
    std::vector<zx::event> release_fences, PresentId present_id) {
  present_id = present_id == kInvalidPresentId ? scheduling::GetNextPresentId() : present_id;
  CallWhenFrameSchedulerAvailable([session_id, present_information = std::move(present_information),
                                   release_fences = std::move(release_fences),
                                   present_id](FrameScheduler* frame_scheduler) mutable {
    frame_scheduler->RegisterPresent(session_id, std::move(present_information),
                                     std::move(release_fences), present_id);
  });
  return present_id;
}

void DelegatingFrameScheduler::ScheduleUpdateForSession(zx::time presentation_time,
                                                        SchedulingIdPair id_pair) {
  CallWhenFrameSchedulerAvailable([presentation_time, id_pair](FrameScheduler* frame_scheduler) {
    frame_scheduler->ScheduleUpdateForSession(presentation_time, id_pair);
  });
}

void DelegatingFrameScheduler::GetFuturePresentationInfos(
    zx::duration requested_prediction_span, GetFuturePresentationInfosCallback callback) {
  FX_DCHECK(callback);
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

void DelegatingFrameScheduler::RemoveSession(SessionId session_id) {
  CallWhenFrameSchedulerAvailable([session_id](FrameScheduler* frame_scheduler) {
    frame_scheduler->RemoveSession(session_id);
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

void DelegatingFrameScheduler::SetFrameScheduler(
    const std::shared_ptr<FrameScheduler>& frame_scheduler) {
  FX_CHECK(!frame_scheduler_) << "DelegatingFrameScheduler can only be set once.";
  FX_CHECK(frame_scheduler) << "DelegatingFrameScheduler cannot be set to a null value.";
  frame_scheduler_ = frame_scheduler;
  for (auto& callback : call_when_frame_scheduler_available_callbacks_) {
    callback(frame_scheduler_.get());
  }
  call_when_frame_scheduler_available_callbacks_.clear();
}

}  // namespace scheduling
