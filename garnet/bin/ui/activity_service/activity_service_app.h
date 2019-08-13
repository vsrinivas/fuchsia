// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ACTIVITY_SERVICE_ACTIVITY_SERVICE_APP_H_
#define GARNET_BIN_UI_ACTIVITY_SERVICE_ACTIVITY_SERVICE_APP_H_

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>

#include <map>
#include <memory>

#include <src/lib/fxl/macros.h>

#include "garnet/bin/ui/activity_service/activity_service_tracker_connection.h"

namespace activity_service {

class ActivityServiceApp {
 public:
  explicit ActivityServiceApp(std::unique_ptr<StateMachineDriver> state_machine_driver,
                              async_dispatcher_t* dispatcher)
      : state_machine_driver_(std::move(state_machine_driver)), dispatcher_(dispatcher) {}

  void AddTrackerBinding(fidl::InterfaceRequest<fuchsia::ui::activity::Tracker> request);

  // Exposed for testing.
  std::vector<const ActivityServiceTrackerConnection*> tracker_bindings() const {
    std::vector<const ActivityServiceTrackerConnection*> vec;
    vec.reserve(tracker_bindings_.size());
    for (const auto& entry : tracker_bindings_) {
      vec.push_back(entry.second.get());
    }
    return vec;
  }

 private:
  std::unique_ptr<StateMachineDriver> state_machine_driver_;
  async_dispatcher_t* dispatcher_;

  std::map<zx::unowned_channel, std::unique_ptr<ActivityServiceTrackerConnection>>
      tracker_bindings_;
};

}  // namespace activity_service

#endif  // GARNET_BIN_UI_ACTIVITY_SERVICE_ACTIVITY_SERVICE_APP_H_
