// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_TIMEKEEPER_ASYNC_TEST_CLOCK_H_
#define LIB_TIMEKEEPER_ASYNC_TEST_CLOCK_H_

#include <limits>

#include <lib/async/dispatcher.h>
#include <lib/timekeeper/clock.h>

namespace timekeeper {

// Implementation of |Clock| using an |async_dispatcher_t|. This class also
// ensures that every clock is strictly increasing.
class AsyncTestClock : public Clock {
 public:
  AsyncTestClock(async_dispatcher_t* dispatcher);
  ~AsyncTestClock() override;

 private:
  zx_status_t GetTime(zx_clock_t clock_id, zx_time_t* time) const override;
  zx_time_t GetMonotonicTime() const override;

  async_dispatcher_t* const dispatcher_;
  mutable zx_time_t last_returned_value_ =
      std::numeric_limits<zx_time_t>::min();
};

}  // namespace timekeeper

#endif  // LIB_TIMEKEEPER_ASYNC_TEST_CLOCK_H_
