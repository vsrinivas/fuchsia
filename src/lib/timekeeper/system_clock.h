// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_TIMEKEEPER_SYSTEM_CLOCK_H_
#define SRC_LIB_TIMEKEEPER_SYSTEM_CLOCK_H_

#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/utc.h>

#include "src/lib/timekeeper/clock.h"

namespace timekeeper {

// Implementation of |Clock| using the clock related syscalls and UTC clock passed
// to the process on launch.
class SystemClock : public Clock {
 private:
  zx_status_t GetUtcTime(zx_time_t* time) const override { return utc_clock_->read(time); }
  zx_time_t GetMonotonicTime() const override { return zx_clock_get_monotonic(); }

  zx::unowned_clock utc_clock_ = zx::unowned_clock(zx_utc_reference_get());
};

}  // namespace timekeeper

#endif  // SRC_LIB_TIMEKEEPER_SYSTEM_CLOCK_H_
