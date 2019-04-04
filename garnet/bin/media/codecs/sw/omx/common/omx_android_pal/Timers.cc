// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utils/Timers.h>

#include <zircon/syscalls.h>

#include <cassert>

nsecs_t systemTime(int clock) {
  if (clock != SYSTEM_TIME_MONOTONIC) {
    assert(clock == SYSTEM_TIME_MONOTONIC);
    return 0;
  }
  return zx_clock_get_monotonic();
}
