// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_TIMEKEEPER_ASYNC_TEST_CLOCK_H_
#define SRC_LIB_TIMEKEEPER_ASYNC_TEST_CLOCK_H_

#include <lib/async/dispatcher.h>

#include "src/lib/timekeeper/monotonic_test_clock_base.h"

namespace timekeeper {

// Implementation of |Clock| using an |async_dispatcher_t|. This class also
// ensures that every clock is strictly increasing.
class AsyncTestClock : public MonotonicTestClockBase {
 public:
  AsyncTestClock(async_dispatcher_t* dispatcher);
  ~AsyncTestClock() override;
};

}  // namespace timekeeper

#endif  // SRC_LIB_TIMEKEEPER_ASYNC_TEST_CLOCK_H_
