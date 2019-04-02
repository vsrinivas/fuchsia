// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETEMUL_RUNNER_FORMAT_H_
#define GARNET_BIN_NETEMUL_RUNNER_FORMAT_H_

#include <zircon/time.h>
#include <iostream>

namespace netemul {
namespace internal {

void FormatTime(std::ostream* stream, zx_time_t timestamp);

}  // namespace internal
}  // namespace netemul

#endif  // GARNET_BIN_NETEMUL_RUNNER_FORMAT_H_
