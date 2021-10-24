// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <kernel/loop_limiter.h>

namespace {

bool loop_limiter_test() {
  BEGIN_TEST;

  // Timeout has occurred.
  EXPECT_TRUE(LoopLimiter<1>::WithDuration(ZX_TIME_INFINITE_PAST).Exceeded());
  EXPECT_TRUE(LoopLimiter<1>::WithDuration(-1).Exceeded());
  EXPECT_TRUE(LoopLimiter<1>::WithDuration(0).Exceeded());

  // Way out in the future.
  EXPECT_FALSE(LoopLimiter<1>::WithDuration(1000000000000).Exceeded());
  EXPECT_FALSE(LoopLimiter<1>::WithDuration(ZX_TIME_INFINITE).Exceeded());

  // Happy-cases.
  {
    auto limiter = LoopLimiter<1>::WithDuration(1);
    while (!limiter.Exceeded()) {
      arch::Yield();
    }
  }
  {
    auto limiter = LoopLimiter<1>::WithDuration(100);
    while (!limiter.Exceeded()) {
      arch::Yield();
    }
  }
  {
    auto limiter = LoopLimiter<100>::WithDuration(1);
    while (!limiter.Exceeded()) {
      arch::Yield();
    }
  }
  {
    auto limiter = LoopLimiter<100>::WithDuration(100);
    while (!limiter.Exceeded()) {
      arch::Yield();
    }
  }

  END_TEST;
}

}  // anonymous namespace

UNITTEST_START_TESTCASE(loop_limiter_tests)
UNITTEST("loop limitier", loop_limiter_test)
UNITTEST_END_TESTCASE(loop_limiter_tests, "loop_limiter", "loop limiter tests")
