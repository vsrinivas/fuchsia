// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_TIMEKEEPER_SYSTEM_CLOCK_H_
#define SRC_LEDGER_LIB_TIMEKEEPER_SYSTEM_CLOCK_H_

#include <lib/zx/time.h>

#include "src/ledger/lib/timekeeper/clock.h"

namespace ledger {

// Implementation of |Clock| using the clock related syscalls.
class SystemClock : public Clock {
 private:
  zx_status_t GetTime(zx_clock_t clock_id, zx_time_t* time) const override {
    return zx_clock_get(clock_id, time);
  }
  zx_time_t GetMonotonicTime() const override { return zx_clock_get_monotonic(); }
};

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_TIMEKEEPER_SYSTEM_CLOCK_H_
