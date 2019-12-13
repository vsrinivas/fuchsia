// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_TIMEKEEPER_CLOCK_H_
#define SRC_LEDGER_LIB_TIMEKEEPER_CLOCK_H_

#include <lib/zx/time.h>

namespace ledger {

// Abstraction over the clock.
//
// This class allows to retrieve the current time for any supported clock id.
// This being a class, it allows to inject custom behavior for tests.
class Clock {
 public:
  Clock() = default;
  virtual ~Clock() = default;
  Clock(const Clock&) = delete;
  Clock& operator=(const Clock&) = delete;

  // Returns the current time for |kClockId|. See |zx_clock_get|.
  template <zx_clock_t kClockId>
  zx_status_t Now(zx::basic_time<kClockId>* result) const {
    zx_time_t time;
    zx_status_t status = GetTime(kClockId, &time);
    *result = zx::basic_time<kClockId>(time);
    return status;
  }

  // Returns the current monotonic time. See |zx_clock_get_monotonic|.
  zx::time Now() const { return zx::time(GetMonotonicTime()); }

 protected:
  // Returns the current time for |kClockId|. See |zx_clock_get|.
  virtual zx_status_t GetTime(zx_clock_t clock_id, zx_time_t* time) const = 0;
  // Returns the current monotonic time. See |zx_clock_get_monotonic|.
  virtual zx_time_t GetMonotonicTime() const = 0;
};

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_TIMEKEEPER_CLOCK_H_
