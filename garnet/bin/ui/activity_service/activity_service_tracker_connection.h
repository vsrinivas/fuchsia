// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ACTIVITY_SERVICE_ACTIVITY_SERVICE_TRACKER_CONNECTION_H_
#define GARNET_BIN_UI_ACTIVITY_SERVICE_ACTIVITY_SERVICE_TRACKER_CONNECTION_H_

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <inttypes.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <random>
#include <set>

#include <src/lib/fxl/macros.h>

#include "garnet/bin/ui/activity_service/common.h"
#include "garnet/bin/ui/activity_service/state_machine_driver.h"

namespace activity_service {

// ActivityServiceTrackerConnection is the server-side implementation of the
// activity service's Tracker FIDL interface.
//
// One instance of ActivityServiceTrackerConnection is created to manage the
// connection with a single client.
class ActivityServiceTrackerConnection : public fuchsia::ui::activity::Tracker {
 public:
  ActivityServiceTrackerConnection(StateMachineDriver* state_machine_driver,
                                   async_dispatcher_t* dispatcher,
                                   fidl::InterfaceRequest<fuchsia::ui::activity::Tracker> request,
                                   uint32_t random_seed)
      : state_machine_driver_(state_machine_driver),
        dispatcher_(dispatcher),
        random_(random_seed),
        binding_(this, std::move(request), dispatcher) {}

  ~ActivityServiceTrackerConnection() { Stop(); }

  // Cleans up any resources owned by the instance, including terminating all ongoing activities.
  void Stop();

  void set_error_handler(fit::function<void(zx_status_t)> callback) {
    binding_.set_error_handler(std::move(callback));
  }

  // fuchsia::ui::activity::Tracker API
  virtual void ReportDiscreteActivity(fuchsia::ui::activity::DiscreteActivity activity,
                                      zx_time_t time);
  virtual void StartOngoingActivity(fuchsia::ui::activity::OngoingActivity activity, zx_time_t time,
                                    StartOngoingActivityCallback callback);
  virtual void EndOngoingActivity(OngoingActivityId id, zx_time_t time);

 private:
  OngoingActivityId GenerateActivityId() { return static_cast<OngoingActivityId>(random_()); }

  StateMachineDriver* const state_machine_driver_;

  async_dispatcher_t* const dispatcher_;

  std::default_random_engine random_;

  std::set<OngoingActivityId> ongoing_activities_;

  fidl::Binding<fuchsia::ui::activity::Tracker> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ActivityServiceTrackerConnection);
};

}  // namespace activity_service

#endif  // GARNET_BIN_UI_ACTIVITY_SERVICE_ACTIVITY_SERVICE_TRACKER_CONNECTION_H_
