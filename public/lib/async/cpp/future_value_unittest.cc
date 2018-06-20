// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/async/cpp/future_value.h"

#include "garnet/public/lib/gtest/test_with_loop.h"
#include "lib/fxl/logging.h"

#include <string>

namespace modular {

namespace {

class FutureValueTest : public gtest::TestWithLoop {
 protected:
  void SetExpected(int expected) { expected_ = expected; }

  void Done() { ++count_; }

 private:
  int count_ = 0;
  int expected_ = 1;
};

TEST_F(FutureValueTest, SetValueAndGet) {
  FutureValue<int> fi;
  fi.SetValue(10);
  ASSERT_EQ(fi.get(), 10);
}

TEST_F(FutureValueTest, Assign) {
  FutureValue<int> fi;
  fi = 10;
  EXPECT_EQ(fi.get(), 10);
}

TEST_F(FutureValueTest, OnValue) {
  FutureValue<int> fi;

  bool called;
  fi.OnValue([&](const int& value) {
    EXPECT_EQ(value, 10);
    Done();
    called = true;
  });

  fi = 10;

  RunLoopUntilIdle();
  EXPECT_TRUE(caled);
}

}  // namespace
}  // namespace modular
