// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include <lib/async/time.h>
#include <lib/timekeeper/async_test_clock.h>

namespace timekeeper {

AsyncTestClock::AsyncTestClock(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {}

AsyncTestClock::~AsyncTestClock() = default;

zx_status_t AsyncTestClock::GetTime(zx_clock_t clock_id,
                                    zx_time_t* time) const {
  *time = async_now(dispatcher_);
  *time = std::max(*time, last_returned_value_ + 1);
  last_returned_value_ = *time;
  return ZX_OK;
}

zx_time_t AsyncTestClock::GetMonotonicTime() const {
  zx_time_t result;
  GetTime(0, &result);
  return result;
}

}  // namespace timekeeper
