// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_TIMEKEEPER_TEST_LOOP_TEST_CLOCK_H_
#define SRC_LEDGER_LIB_TIMEKEEPER_TEST_LOOP_TEST_CLOCK_H_

#include <lib/async-testing/test_loop.h>

#include "src/ledger/lib/timekeeper/monotonic_test_clock_base.h"

namespace ledger {

// Implementation of |Clock| using a |async::TestLoop|. This class also
// ensures that every clock is strictly increasing.
class TestLoopTestClock : public MonotonicTestClockBase {
 public:
  TestLoopTestClock(async::TestLoop* test_loop);
  ~TestLoopTestClock() override;
};

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_TIMEKEEPER_TEST_LOOP_TEST_CLOCK_H_
