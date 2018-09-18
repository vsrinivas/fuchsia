// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_TIMEKEEPER_CLOCK_IMPL_H_
#define LIB_TIMEKEEPER_CLOCK_IMPL_H_

#include <lib/timekeeper/clock.h>

namespace timekeeper {

// Implementation of |Clock| using the clock related syscalls.
class ClockImpl : public Clock {
 private:
  zx_status_t GetTime(zx_clock_t clock_id, zx_time_t* time) const override;
  zx_time_t GetMonotonicTime() const override;
};

}  // namespace timekeeper

#endif  // LIB_TIMEKEEPER_CLOCK_IMPL_H_
