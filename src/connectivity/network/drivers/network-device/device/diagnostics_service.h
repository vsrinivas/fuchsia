// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DIAGNOSTICS_SERVICE_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DIAGNOSTICS_SERVICE_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include <atomic>

#include "definitions.h"

namespace network::testing {
class NetworkDeviceTest;
}  // namespace network::testing

namespace network {
class DiagnosticsService : public fidl::WireServer<netdev::Diagnostics> {
 public:
  DiagnosticsService();
  void LogDebugInfoToSyslog(LogDebugInfoToSyslogCompleter::Sync& completer) override;

  // Binds |server_end| to the diagnostics service.
  //
  // All requests are served from a dedicated diagnostics thread.
  void Bind(fidl::ServerEnd<netdev::Diagnostics> server_end);

 private:
  friend testing::NetworkDeviceTest;
  async::Loop loop_;
  std::atomic<bool> thread_started_ = false;

  // Functions hooks for override in tests.
  fit::function<void()> trigger_stack_trace_;
};
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DIAGNOSTICS_SERVICE_H_
