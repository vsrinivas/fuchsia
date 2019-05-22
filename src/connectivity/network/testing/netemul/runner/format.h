// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_FORMAT_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_FORMAT_H_

#include <zircon/time.h>

#include <iostream>

namespace netemul {
namespace internal {

// Format the time to monotomic and send it to |stream|.
void FormatTime(std::ostream* stream, const zx_time_t timestamp);

// Format the tags and send it to |stream|.
void FormatTags(std::ostream* stream, const std::vector<std::string>& tags);

// Format the log level and send it to |stream|.
void FormatLogLevel(std::ostream* stream, const int32_t severity);

}  // namespace internal
}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_FORMAT_H_
