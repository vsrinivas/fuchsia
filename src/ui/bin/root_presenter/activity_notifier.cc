// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/activity_notifier.h"

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <optional>

#include "src/lib/fxl/logging.h"

namespace root_presenter {

using fuchsia::ui::activity::DiscreteActivity;
using fuchsia::ui::activity::GenericActivity;
using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::MediaButtonsEvent;

ActivityNotifierImpl::ActivityNotifierImpl(async_dispatcher_t* dispatcher, zx::duration interval,
                                           sys::ComponentContext& context)
    : dispatcher_(dispatcher), interval_(interval) {
  activity_tracker_service_ = context.svc()->Connect<fuchsia::ui::activity::Tracker>();
  activity_tracker_service_.set_error_handler([this](zx_status_t error) {
    FXL_LOG(ERROR) << "Activity service died (" << zx_status_get_string(error) << "), no longer "
                   << "sending activity events.";
    activity_tracker_service_ = nullptr;
  });
}

void ActivityNotifierImpl::ReceiveInputEvent(const InputEvent& event) {
  if (auto activity = ActivityForInputEvent(event)) {
    MaybeEnqueueActivity(*std::move(activity));
  }
}

void ActivityNotifierImpl::ReceiveMediaButtonsEvent(const MediaButtonsEvent& event) {
  if (auto activity = ActivityForMediaButtonsEvent(event)) {
    MaybeEnqueueActivity(*std::move(activity));
  }
}

void ActivityNotifierImpl::MaybeEnqueueActivity(fuchsia::ui::activity::DiscreteActivity activity) {
  if (pending_activity_ || !activity_tracker_service_) {
    return;
  }
  pending_activity_ = std::move(activity);
  if (!notify_task_.is_pending()) {
    notify_task_.Post(dispatcher_);
  }
}

void ActivityNotifierImpl::NotifyForPendingActivity() {
  if (pending_activity_ && activity_tracker_service_) {
    auto now = async::Now(dispatcher_);
    activity_tracker_service_->ReportDiscreteActivity(
        std::move(*pending_activity_), now.get(), [this]() {
            notify_task_.PostDelayed(dispatcher_, interval_);
            pending_activity_ = std::nullopt;
        });
  } else {
    // |notify_task_| is intentionally not re-scheduled if no input was received recently. It
    // will be scheduled again on the next call to |ReceiveInput|.
    pending_activity_ = std::nullopt;
  }
}

std::optional<DiscreteActivity> ActivityNotifierImpl::ActivityForInputEvent(
    const InputEvent& event) {
  switch (event.Which()) {
    case InputEvent::Tag::kKeyboard:
    case InputEvent::Tag::kPointer: {
      GenericActivity generic;
      return DiscreteActivity::WithGeneric(std::move(generic));
    }
    case InputEvent::Tag::kFocus:
    default: {
      return std::nullopt;
    }
  }
}

std::optional<DiscreteActivity> ActivityNotifierImpl::ActivityForMediaButtonsEvent(
    const MediaButtonsEvent& event) {
  GenericActivity generic;
  return DiscreteActivity::WithGeneric(std::move(generic));
}

}  // namespace root_presenter
