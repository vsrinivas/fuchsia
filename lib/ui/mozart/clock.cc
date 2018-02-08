// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/mozart/clock.h"

#include <zircon/syscalls.h>

namespace mz {

Clock::Clock() = default;
Clock::~Clock() = default;

zx_time_t Clock::GetNanos() {
  return zx_clock_get(ZX_CLOCK_MONOTONIC);
}

}  // namespace mz
