// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_FORMAT_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_FORMAT_H_

#include <zircon/time.h>

#include <iostream>

namespace netemul {
namespace internal {

void FormatTime(std::ostream* stream, zx_time_t timestamp);

}  // namespace internal
}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_FORMAT_H_
