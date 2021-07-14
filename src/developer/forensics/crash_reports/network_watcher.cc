// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/network_watcher.h"

#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>

namespace forensics::crash_reports {

NetworkWatcher::NetworkWatcher(async_dispatcher_t* dispatcher,
                               const sys::ServiceDirectory& services) {
  fuchsia::net::interfaces::StatePtr state;
  zx_status_t status = services.Connect(state.NewRequest(dispatcher));
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to connect to " << fuchsia::net::interfaces::State::Name_
                            << "; cannot watch for network reachability status";
    return;
  }

  fuchsia::net::interfaces::WatcherPtr watcher;
  state->GetWatcher(fuchsia::net::interfaces::WatcherOptions(), watcher.NewRequest(dispatcher));

  watcher_ = std::make_unique<net::interfaces::ReachabilityWatcher>(
      std::move(watcher), [this](auto reachable) {
        if (reachable.is_error()) {
          FX_LOGS(ERROR) << "Network reachability watcher encountered unrecoverable error: "
                         << net::interfaces::ReachabilityWatcher::error_get_string(
                                reachable.error());
          return;
        }
        reachable_ = reachable.value();
        for (const auto& on_reachable : callbacks_) {
          on_reachable(reachable_.value());
        }
      });
}

void NetworkWatcher::Register(fit::function<void(bool)> on_reachable) {
  if (reachable_.has_value()) {
    on_reachable(reachable_.value());
  }
  callbacks_.push_back(std::move(on_reachable));
}

}  // namespace forensics::crash_reports
