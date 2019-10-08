// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/recursion_detector.h>
#include <zxtest/zxtest.h>

namespace {

// These objects exist solely so that the test code below can get a pointer to their address,
// which is passed into RecursionDetector.Enter().
int object;
int object2;

}  // namespace

TEST(RecursionDetector, EnterSameObjectTwiceResultsInNoGuard) {
  RecursionDetector rd;

  auto guard = rd.Enter(&object);
  ASSERT_TRUE(guard.has_value());

  auto guard2 = rd.Enter(&object2);
  ASSERT_TRUE(guard2.has_value());

  auto no_guard = rd.Enter(&object);
  ASSERT_FALSE(no_guard.has_value());
}

TEST(RecursionDetector, GuardObjectPopsSeenObjectsOnScopeExit) {
  RecursionDetector rd;

  auto guard = rd.Enter(&object);
  ASSERT_TRUE(guard.has_value());

  {
    auto guard2 = rd.Enter(&object2);
    ASSERT_TRUE(guard2.has_value());
  }

  {
    auto new_guard2 = rd.Enter(&object2);
    ASSERT_TRUE(new_guard2.has_value());
  }
}
