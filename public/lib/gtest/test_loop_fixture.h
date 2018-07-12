// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_GTEST_TEST_LOOP_FIXTURE_H_
#define LIB_GTEST_TEST_LOOP_FIXTURE_H_

#include <functional>

#include <lib/async-testutils/test_loop.h>
#include <lib/fit/function.h>

#include "gtest/gtest.h"
#include "lib/fxl/macros.h"

namespace gtest {

// An extension of gtest's testing::Test class which sets up a message loop for
// the test.
//
// Example:
//
//   class FooTest : public ::gtest::TestLoopFixture { /* ... */ };
//
//   TEST_F(FooTest, TestCase) {
//
//     // Initialize an object with the underlying loop's dispatcher.
//     Foo foo(dispatcher());
//
//     /* Call a method of |foo| that posts a delayed task. */
//
//     RunLoopFor(zx::sec(17));
//
//     /* Make assertions about the state of the test case, say about |foo|. */
//   }
class TestLoopFixture : public ::testing::Test {
 protected:
  TestLoopFixture();
  ~TestLoopFixture();

  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }

  // Returns the current fake clock time.
  zx::time Now() { return loop_.Now(); }

  // Advances the fake clock time by |time|, if |time| is greater than the
  // current time; else, nothing happens.
  void AdvanceTimeTo(zx::time time) { loop_.AdvanceTimeTo(time); }

  // Advances the fake clock time by |delta|.
  void AdvanceTimeBy(zx::duration delta) { loop_.AdvanceTimeBy(delta); }

  // Dispatches all waits and all tasks posted to the message loop with
  // deadlines up until |deadline|, progressively advancing the fake clock.
  // Returns true iff any tasks or waits were invoked during the run.
  bool RunLoopUntil(zx::time deadline) { return loop_.RunUntil(deadline); }

  // Dispatches all waits and all tasks posted to the message loop with
  // deadlines up until |duration| from the current time, progressively
  // advancing the fake clock.
  // Returns true iff any tasks or waits were invoked during the run.
  bool RunLoopFor(zx::duration duration) { return loop_.RunFor(duration); };

  // Dispatches all waits and all tasks posted to the message loop with
  // deadlines up until the current time, progressively advancing the fake
  // clock.
  // Returns true iff any tasks or waits were invoked during the run.
  bool RunLoopUntilIdle() { return loop_.RunUntilIdle(); }

  // Repeatedly runs the loop by |increment| until nothing further is left to
  // dispatch.
  void RunLoopRepeatedlyFor(zx::duration increment);

  // Quits the message loop. If called while running, it will immediately
  // exit and dispatch no further tasks or waits; if called before running,
  // then next call to run will immediately exit. Further calls to run will
  // continue to dispatch.
  void QuitLoop() { loop_.Quit(); }

  // A callback that quits the message loop when called.
  fit::closure QuitLoopClosure() {
    return [this] { loop_.Quit(); };
  }

 private:
  // The test message loop for the test fixture.
  async::TestLoop loop_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestLoopFixture);
};

}  // namespace gtest

#endif  // LIB_GTEST_TEST_LOOP_FIXTURE_H_
