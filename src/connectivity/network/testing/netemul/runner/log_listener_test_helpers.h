// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_LOG_LISTENER_TEST_HELPERS_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_LOG_LISTENER_TEST_HELPERS_H_

#include <fuchsia/logger/cpp/fidl.h>

namespace netemul {
namespace testing {

constexpr uint64_t kDummyTid = 0xAA;
constexpr uint64_t kDummyPid = 0xBB;
constexpr int64_t kDummyTime = 0xCCAACC;
constexpr int32_t kDummySeverity = 3;

// Create a test log message.
fuchsia::logger::LogMessage CreateLogMessage(std::vector<std::string> tags,
                                             std::string message);

}  // namespace testing
}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_LOG_LISTENER_TEST_HELPERS_H_
