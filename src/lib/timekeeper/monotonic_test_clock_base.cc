// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/timekeeper/monotonic_test_clock_base.h"

#include <algorithm>
#include <type_traits>

namespace timekeeper {

// Using an arbitrary, non-hour offset to avoid uncaught bugs from, for example, the monotonic and
// UTC clocks passing midnight at the same time.
constexpr zx::duration kUtcOffsetFromMonotonic = zx::hour(53) + zx::min(14) + zx::sec(52);

MonotonicTestClockBase::MonotonicTestClockBase(fit::function<zx_time_t()> clock)
    : clock_(std::move(clock)) {}

MonotonicTestClockBase::~MonotonicTestClockBase() = default;

zx_status_t MonotonicTestClockBase::GetUtcTime(zx_time_t* time) const {
  *time = kUtcOffsetFromMonotonic.get() + GetMonotonicTime();
  return ZX_OK;
}

zx_time_t MonotonicTestClockBase::GetMonotonicTime() const {
  zx_time_t result = std::max(clock_(), last_returned_value_ + 1);
  last_returned_value_ = result;
  return result;
}

}  // namespace timekeeper
