// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(alhaad): This file has been copied from Ledger and Mozart. Reconcile
// all copies to a common place.

#ifndef APPS_MODULAR_LIB_TESTING_TEST_WITH_MESSAGE_LOOP_H_
#define APPS_MODULAR_LIB_TESTING_TEST_WITH_MESSAGE_LOOP_H_

#include <functional>

#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {
namespace testing {

// An implementation of gtest's testing::Test class, that can be further
// sub-classed by test fixtures that want to use FIDL.
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
//
// |TestWithMessageLoop| creates a message loop that installs itself as a thread
// local singleton and is used by FIDL bindings to get asynchronous waiters.
// |TestWithMessageLoop| also provides methods to quit from the message loop.
class TestWithMessageLoop : public ::testing::Test {
 protected:
  TestWithMessageLoop() {}

  // Runs the loop for at most |timeout|. Returns |true| if the timeout has been
  // reached.
  bool RunLoopWithTimeout(
      ftl::TimeDelta timeout = ftl::TimeDelta::FromSeconds(1));

  // Runs the loop until the condition returns true or the timeout is reached.
  // Returns |true| if the condition was met, and |false| if the timeout was
  // reached.
  bool RunLoopUntil(std::function<bool()> condition,
                    ftl::TimeDelta timeout = ftl::TimeDelta::FromSeconds(1));

  // Creates a closure that quits the test message loop when executed.
  std::function<void()> MakeQuitTask();

  mtl::MessageLoop message_loop_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(TestWithMessageLoop);
};

}  // namespace testing
}  // namespace modular

#endif  // APPS_MODULAR_LIB_TESTING_TEST_WITH_MESSAGE_LOOP_H_
