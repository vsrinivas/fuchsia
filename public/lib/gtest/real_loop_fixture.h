// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_GTEST_REAL_LOOP_FIXTURE_H_
#define LIB_GTEST_REAL_LOOP_FIXTURE_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include "gtest/gtest.h"
#include "lib/fxl/macros.h"

namespace gtest {

// An extension of gtest's testing::Test class which sets up a message loop,
// async::Loop, for the test. This fixture is meant to be used for
// multi-process tests.
//
// This allows, for example, a test to conveniently excercise FIDL, as FIDL
// bindings post waits to the thread-local dispatcher.
//
// Example:
//
//   #include "foo.fidl.h"
//
//   class TestFoo : public Foo {
//    public:
//      explicit TestFoo(InterfaceRequest<Foo> request)
//          : binding_(this, std::move(request) {}
//
//        // Foo implementation here.
//
//    private:
//     Binding<Foo> binding_;
//   };
//
//   // Creates a gtest fixture that creates a message loop on this thread.
//   class TestBar : public RealLoopFixture { /* ... */ };
//
//   TEST_F(TestBar, TestCase) {
//     // Do all FIDL-y stuff here and asynchronously quit.
//
//     RunLoop();
//
//     // Check results from FIDL-y stuff here.
//   }
class RealLoopFixture : public ::testing::Test {
 protected:
  RealLoopFixture();
  ~RealLoopFixture() override;

  // Returns the loop's asynchronous dispatch interface.
  async_t* dispatcher();

  // Runs the loop until it is exited.
  void RunLoop();

  // Runs the loop for at most |timeout|. Returns |true| if the timeout has been
  // reached.
  bool RunLoopWithTimeout(zx::duration timeout = zx::sec(1));

  // Runs the loop until the condition returns true or the timeout is reached.
  // Returns |true| if the condition was met, and |false| if the timeout was
  // reached.
  // TODO(qsr): When existing usage have been migrated to
  // |RunLoopWithTimeoutOrUntil|, remove the timeout from this method.
  bool RunLoopUntil(fit::function<bool()> condition,
                    zx::duration step = zx::msec(10));

  // Runs the loop until the condition returns true or the timeout is reached.
  // Returns |true| if the condition was met, and |false| if the timeout was
  // reached.
  bool RunLoopWithTimeoutOrUntil(
      fit::function<bool()> condition,
      zx::duration timeout = zx::sec(1),
      zx::duration step = zx::msec(10));

  // Runs the message loop until idle.
  void RunLoopUntilIdle();

  // Quits the loop.
  void QuitLoop();

  // Creates a closure that quits the test message loop when executed.
  fit::closure QuitLoopClosure();

 private:
  // The message loop for the test.
  async::Loop loop_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RealLoopFixture);
};

}  // namespace gtest

#endif  // LIB_GTEST_REAL_LOOP_FIXTURE_H_
