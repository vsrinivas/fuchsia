// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/timekeeper/test_loop_test_clock.h"

#include <lib/async/time.h>

namespace ledger {
namespace {

fit::function<zx_time_t()> GetTimeFactory(async::TestLoop* test_loop) {
  return [test_loop] {
    zx_time_t result = async_now(test_loop->dispatcher());
    test_loop->AdvanceTimeByEpsilon();
    return result;
  };
}

}  // namespace

TestLoopTestClock::TestLoopTestClock(async::TestLoop* test_loop)
    : MonotonicTestClockBase(GetTimeFactory(test_loop)) {}

TestLoopTestClock::~TestLoopTestClock() = default;

}  // namespace ledger
