// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_LOG_LISTENER_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_LOG_LISTENER_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include "src/developer/system_monitor/lib/dockyard/dockyard.h"

namespace harvester {

// Retrieves structured logs from ArchiveAccessor.
class LogListener {
 public:
  explicit LogListener(const std::shared_ptr<sys::ServiceDirectory>& services);

  // Set up a fidl connection with ArchiveAccessor and trigger callback for each
  // new structured log batch.
  //
  // The content_callback will be passed a vector of JSON arrays containing
  // structured logs. E.g. {"[{log: data}]", "[{log: data}]"}.
  //
  // See https://fuchsia.dev/fuchsia-src/reference/diagnostics/logs/access for
  // reference on log structure.
  //
  // Return:
  // A promise that resolves when no more logs are available or an error is
  // recieved.
  fit::promise<> Listen(
      std::function<void(std::vector<const std::string>)> content_callback);

 private:
  fuchsia::diagnostics::BatchIteratorPtr iterator_;
  fuchsia::diagnostics::StreamParameters stream_parameters_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  // Recursively calls BatchIterator.GetNext() to asynchronously recieve each
  // new batch of logs.
  void GetLogData(
      std::function<void(std::vector<const std::string>)> content_callback,
      fit::completer<>&& completer);
};

}  // namespace harvester

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_LOG_LISTENER_H_
