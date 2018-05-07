// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_GTEST_TEST_WITH_MESSAGE_LOOP_H_
#define LIB_GTEST_TEST_WITH_MESSAGE_LOOP_H_

#include <functional>

#include "gtest/gtest.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/time/time_delta.h"

namespace gtest {

// An extension of gtest's testing::Test class which sets up a message loop of
// the test.
//
// This allows the test, for example, to excercise FIDL, as FIDL bindings use
// the thread-local message loop to get async waiters.
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
//   class TestBar : public TestWithMessageLoop {};
//
//   TEST_F(TestBar, TestCase) {
//     // Do all FIDL-y stuff here.
//
//     EXPECT_FALSE(RunLoopWithTimeout());
//
//     // Check results from FIDL-y stuff here.
//   }
class TestWithMessageLoop : public ::testing::Test {
 protected:
  TestWithMessageLoop();
  ~TestWithMessageLoop() override;

  // Runs the loop until it is exited.
  void RunLoop();

  // Runs the loop for at most |timeout|. Returns |true| if the timeout has been
  // reached.
  bool RunLoopWithTimeout(
      fxl::TimeDelta timeout = fxl::TimeDelta::FromSeconds(1));

  // Runs the loop until the condition returns true or the timeout is reached.
  // Returns |true| if the condition was met, and |false| if the timeout was
  // reached.
  // TODO(qsr): When existing usage have been migrated to
  // |RunLoopUntilWithTimeout|, remove the timeout from this method.
  bool RunLoopUntil(std::function<bool()> condition,
                    fxl::TimeDelta step = fxl::TimeDelta::FromMilliseconds(10));

  // Runs the loop until the condition returns true or the timeout is reached.
  // Returns |true| if the condition was met, and |false| if the timeout was
  // reached.
  bool RunLoopUntilWithTimeout(
      std::function<bool()> condition,
      fxl::TimeDelta timeout = fxl::TimeDelta::FromSeconds(1),
      fxl::TimeDelta step = fxl::TimeDelta::FromMilliseconds(10));

  // Runs the message loop until idle.
  void RunLoopUntilIdle();

  // Creates a closure that quits the test message loop when executed.
  fxl::Closure MakeQuitTask();

  // Creates a closure that quits the test message loop on the first time it's
  // executed. If executed a second time, it does nothing.
  fxl::Closure MakeQuitTaskOnce();

  // The message loop for the test.
  fsl::MessageLoop message_loop_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(TestWithMessageLoop);
};

}  // namespace gtest

#endif  // LIB_GTEST_TEST_WITH_MESSAGE_LOOP_H_
