// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ACTIVITY_ACTIVITY_TRACKER_CONNECTION_H_
#define SRC_UI_BIN_ACTIVITY_ACTIVITY_TRACKER_CONNECTION_H_

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <inttypes.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <set>

#include "src/lib/fxl/macros.h"
#include "src/ui/bin/activity/common.h"
#include "src/ui/bin/activity/state_machine_driver.h"

namespace activity {

// ActivityTrackerConnection is the server-side implementation of the
// activity service's Tracker FIDL interface.
//
// One instance of ActivityTrackerConnection is created to manage the
// connection with a single client.
class ActivityTrackerConnection : public fuchsia::ui::activity::Tracker {
 public:
  ActivityTrackerConnection(StateMachineDriver* state_machine_driver,
                            async_dispatcher_t* dispatcher,
                            fidl::InterfaceRequest<fuchsia::ui::activity::Tracker> request);
  ~ActivityTrackerConnection();

  // Cleans up any resources owned by the instance, including terminating all ongoing activities.
  void Stop();

  void set_error_handler(fit::function<void(zx_status_t)> callback) {
    binding_.set_error_handler(std::move(callback));
  }

  // fuchsia::ui::activity::Tracker API
  virtual void ReportDiscreteActivity(fuchsia::ui::activity::DiscreteActivity activity,
                                      zx_time_t time, ReportDiscreteActivityCallback callback);
  virtual void StartOngoingActivity(OngoingActivityId id,
                                    fuchsia::ui::activity::OngoingActivity activity, zx_time_t time,
                                    StartOngoingActivityCallback callback);
  virtual void EndOngoingActivity(OngoingActivityId id, zx_time_t time,
                                  EndOngoingActivityCallback callback);

 private:
  zx::time last_activity_time_;

  StateMachineDriver* const state_machine_driver_;

  async_dispatcher_t* const dispatcher_;

  std::set<OngoingActivityId> ongoing_activities_;

  fidl::Binding<fuchsia::ui::activity::Tracker> binding_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ActivityTrackerConnection);
};

}  // namespace activity

#endif  // SRC_UI_BIN_ACTIVITY_ACTIVITY_TRACKER_CONNECTION_H_
