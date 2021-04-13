// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_NETWORK_WATCHER_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_NETWORK_WATCHER_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>

#include <vector>

#include "src/connectivity/network/lib/net_interfaces/cpp/reachability.h"

namespace forensics::crash_reports {

// Watches for changes to the network reachability status and calls registered callbacks whenever
// this occurs.
class NetworkWatcher {
 public:
  NetworkWatcher(async_dispatcher_t* dispatcher, const sys::ServiceDirectory& services);

  // Register a callback to be called when the network reachability status changes.
  void Register(::fit::function<void(bool)> on_reachable);

 private:
  std::unique_ptr<net::interfaces::ReachabilityWatcher> watcher_;

  std::vector<::fit::function<void(bool)>> callbacks_;

  std::optional<bool> reachable_;
};

}  // namespace forensics::crash_reports

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_NETWORK_WATCHER_H_
