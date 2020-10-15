// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/time/network_time_service/inspect.h"

namespace network_time_service {

std::string FailureStatusAsString(time_server::Status status) {
  switch (status) {
    case time_server::NOT_SUPPORTED:
      return "not_supported";
    case time_server::BAD_RESPONSE:
      return "bad_response";
    case time_server::NETWORK_ERROR:
      return "network";
    case time_server::OK:
      return "unknown";
  }
}

Inspect::Inspect(inspect::Node root)
    : root_node_(std::move(root)),
      success_count_(root_node_.CreateUint("success_count", 0)),
      failure_node_(root_node_.CreateChild("failure_count")),
      failure_counts_() {}

void Inspect::Success() { success_count_.Add(1); }

void Inspect::Failure(time_server::Status status) {
  if (failure_counts_.find(status) == failure_counts_.end()) {
    failure_counts_.insert(
        std::make_pair(status, failure_node_.CreateUint(FailureStatusAsString(status), 1)));
  } else {
    failure_counts_[status].Add(1);
  }
}

}  // namespace network_time_service
