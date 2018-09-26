// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/timekeeper/monotonic_test_clock_base.h"

#include <algorithm>

namespace timekeeper {

MonotonicTestClockBase::MonotonicTestClockBase(fit::function<zx_time_t()> clock)
    : clock_(std::move(clock)) {}

MonotonicTestClockBase::~MonotonicTestClockBase() = default;

zx_status_t MonotonicTestClockBase::GetTime(zx_clock_t clock_id,
                                            zx_time_t* time) const {
  *time = GetMonotonicTime();
  return ZX_OK;
}

zx_time_t MonotonicTestClockBase::GetMonotonicTime() const {
  zx_time_t result = std::max(clock_(), last_returned_value_ + 1);
  last_returned_value_ = result;
  return result;
}

}  // namespace timekeeper
