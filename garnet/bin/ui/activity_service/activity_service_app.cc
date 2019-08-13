// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/activity_service/activity_service_app.h"

#include <src/lib/fxl/logging.h>

namespace activity_service {

void ActivityServiceApp::AddTrackerBinding(
    fidl::InterfaceRequest<fuchsia::ui::activity::Tracker> request) {
  zx::unowned_channel unowned(request.channel());
  auto conn = std::make_unique<ActivityServiceTrackerConnection>(state_machine_driver_.get(),
                                                                 dispatcher_, std::move(request),
                                                                 zx::clock::get_monotonic().get());
  conn->set_error_handler([this, unowned, cp = conn.get()](zx_status_t status) {
    cp->Stop();
    auto entry = tracker_bindings_.find(unowned);
    if (entry != tracker_bindings_.end()) {
      tracker_bindings_.erase(entry);
    } else {
      FXL_LOG(ERROR) << "Failed to remove binding during cleanup";
    }
  });
  tracker_bindings_.emplace(std::move(unowned), std::move(conn));
}

}  // namespace activity_service
