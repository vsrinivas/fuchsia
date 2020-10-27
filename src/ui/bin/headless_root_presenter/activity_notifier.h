// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_HEADLESS_ROOT_PRESENTER_ACTIVITY_NOTIFIER_H_
#define SRC_UI_BIN_HEADLESS_ROOT_PRESENTER_ACTIVITY_NOTIFIER_H_

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/time.h>

#include <optional>

#include "src/lib/fxl/macros.h"

namespace root_presenter {

// Public interface for receiving input events and possibly notifying the activity service of user
// activity. See ActivityNotifierImpl for details.
class ActivityNotifier {
 public:
  virtual ~ActivityNotifier() {}
  // Receive an input event, possibly notifying the activity service of user activity.
  virtual void ReceiveInputEvent(const fuchsia::ui::input::InputEvent& event) = 0;
  // Receive a media button event, possibly notifying the activity service of user activity.
  virtual void ReceiveMediaButtonsEvent(const fuchsia::ui::input::MediaButtonsEvent& event) = 0;
};

// ActivityNotifier receives user input events and notifies the activity service of user activity
// based on these events.
//
// ActivityNotifier does not send every event to the activity service; instead a task queue is used
// to rate-limit reports to at most |interval|. Since the activity service is concerned about recent
// activity on the scale of minutes/seconds, it's unnecessary to send every input event (which can
// come at intervals of ~milliseconds, e.g. while dragging a cursor), and doing so could have
// performance/power implications.
class ActivityNotifierImpl : public ActivityNotifier {
 public:
  constexpr static zx::duration kDefaultInterval = zx::sec(5);
  ActivityNotifierImpl(async_dispatcher_t* dispatcher, zx::duration interval,
                       sys::ComponentContext& context);

  // ActivityNotifierImpl::Receive*() receive input events and prepares a |pending_activity_| to
  // send to the activity service (if the input event corresponds to user activity).
  // The activity will be delivered later by |notify_task_| (which is scheduled by this method if
  // not already scheduled).
  // If |pending_activity_| is already set, these methods have no effect.
  void ReceiveInputEvent(const fuchsia::ui::input::InputEvent& event) override;
  void ReceiveMediaButtonsEvent(const fuchsia::ui::input::MediaButtonsEvent& event) override;

 private:
  // If there is no |pending_activity_|, sets the pending activity to |activity|
  // and dispatches |notify_task_| to send the activity (unless |notify_task_| was already
  // pending).
  void MaybeEnqueueActivity(fuchsia::ui::activity::DiscreteActivity activity);
  // If there is a |pending_activity_|, notify the activity service of the activity and reset
  // |pending_activity_|.
  void NotifyForPendingActivity();

  static std::optional<fuchsia::ui::activity::DiscreteActivity> ActivityForInputEvent(
      const fuchsia::ui::input::InputEvent& event);
  static std::optional<fuchsia::ui::activity::DiscreteActivity> ActivityForMediaButtonsEvent(
      const fuchsia::ui::input::MediaButtonsEvent& event);

  async_dispatcher_t* const dispatcher_;
  fuchsia::ui::activity::TrackerPtr activity_tracker_service_;
  const zx::duration interval_;
  std::optional<fuchsia::ui::activity::DiscreteActivity> pending_activity_;
  async::TaskClosureMethod<ActivityNotifierImpl, &ActivityNotifierImpl::NotifyForPendingActivity>
      notify_task_{this};

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ActivityNotifierImpl);
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_HEADLESS_ROOT_PRESENTER_ACTIVITY_NOTIFIER_H_
