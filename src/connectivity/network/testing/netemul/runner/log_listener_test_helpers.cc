// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log_listener_test_helpers.h"

namespace netemul {
namespace testing {

fuchsia::logger::LogMessage CreateLogMessage(std::vector<std::string> tags,
                                             std::string message) {
  return fuchsia::logger::LogMessage{.severity = kDummySeverity,
                                     .time = kDummyTime,
                                     .pid = kDummyPid,
                                     .tid = kDummyTid,
                                     .dropped_logs = 0,
                                     .tags = std::move(tags),
                                     .msg = std::move(message)};
}

}  // namespace testing
}  // namespace netemul
