// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_NETWORK_WATCHER_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_NETWORK_WATCHER_H_

#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>

#include <vector>

#include "src/lib/backoff/exponential_backoff.h"

namespace forensics {
namespace crash_reports {

// Notifies interested parties when the network reachability status changes and manages the FIDL
// connection with the service that sends status change events.
//
// fuchsia.netstack.Netstack is expected to be in |services_|.
class NetworkWatcher {
 public:
  NetworkWatcher(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services);

  // Register a callback to be called when the network reachability status changes.
  void Register(::fit::function<void(bool)> on_reachable);

 private:
  void Watch();

  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;

  fuchsia::netstack::NetstackPtr netstack_;

  backoff::ExponentialBackoff backoff_;
  async::TaskClosureMethod<NetworkWatcher, &NetworkWatcher::Watch> watch_task_{this};

  std::vector<::fit::function<void(bool)>> callbacks_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_NETWORK_WATCHER_H_
