// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_TIMEKEEPER_MONOTONIC_TEST_CLOCK_BASE_H_
#define SRC_LEDGER_LIB_TIMEKEEPER_MONOTONIC_TEST_CLOCK_BASE_H_

#include <lib/fit/function.h>

#include <limits>

#include "src/ledger/lib/timekeeper/clock.h"

namespace ledger {

// Abstract implementation of |Clock| that takes a time_t generator and ensure
// that every clock is strictly increasing.
class MonotonicTestClockBase : public Clock {
 public:
  MonotonicTestClockBase(fit::function<zx_time_t()> clock);
  ~MonotonicTestClockBase() override;

 private:
  zx_status_t GetTime(zx_clock_t clock_id, zx_time_t* time) const final;
  zx_time_t GetMonotonicTime() const final;

  fit::function<zx_time_t()> clock_;
  mutable zx_time_t last_returned_value_ = std::numeric_limits<zx_time_t>::min();
};

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_TIMEKEEPER_MONOTONIC_TEST_CLOCK_BASE_H_
