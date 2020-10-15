// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TIME_NETWORK_TIME_SERVICE_INSPECT_H_
#define SRC_SYS_TIME_NETWORK_TIME_SERVICE_INSPECT_H_

#include <fuchsia/time/external/cpp/fidl.h>
#include <lib/sys/inspect/cpp/component.h>

#include <unordered_map>

#include "src/sys/time/lib/network_time/time_server_config.h"

namespace network_time_service {

// Obtain a string representation of status suitable for inspect output.
std::string FailureStatusAsString(time_server::Status status);

// Wrapper around inspect output that tracks successful and failed polls.
class Inspect {
 public:
  explicit Inspect(inspect::Node root);
  // Record a successful poll.
  void Success();
  // Record a failed poll.
  void Failure(time_server::Status status);

 private:
  inspect::Node root_node_;
  inspect::UintProperty success_count_;
  inspect::Node failure_node_;
  std::unordered_map<time_server::Status, inspect::UintProperty> failure_counts_;
};

}  // namespace network_time_service

#endif  // SRC_SYS_TIME_NETWORK_TIME_SERVICE_INSPECT_H_
