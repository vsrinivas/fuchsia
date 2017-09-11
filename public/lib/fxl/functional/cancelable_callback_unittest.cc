// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/functional/cancelable_callback.h"

#include "gtest/gtest.h"

#include "lib/fxl/functional/closure.h"

namespace fxl {
namespace {

void AddBToA(int* a, int b) {
  (*a) += b;
}

TEST(CancelableCallbackTest, Cancel) {
  int count = 0;
  CancelableClosure cancelable([&count] { count++; });

  auto callback = cancelable.callback();
  callback();
  EXPECT_EQ(1, count);

  callback();
  EXPECT_EQ(2, count);

  cancelable.Cancel();
  callback();
  EXPECT_EQ(2, count);
}

TEST(CancelableCallbackTest, MultipleCancel) {
  int count = 0;
  CancelableClosure cancelable([&count] { count++; });

  auto callback1 = cancelable.callback();
  auto callback2 = cancelable.callback();
  cancelable.Cancel();

  callback1();
  EXPECT_EQ(0, count);

  callback2();
  EXPECT_EQ(0, count);

  // Calling Cancel() again has no effect.
  cancelable.Cancel();

  // Calling callback() on a canceled CancelableClosure should return a null
  // callback.
  auto callback3 = cancelable.callback();
  EXPECT_FALSE(callback3);
}

TEST(CancelableCallbackTest, CancelOnDestruction) {
  int count = 0;
  Closure callback;

  {
    CancelableClosure cancelable([&count] { count++; });
    callback = cancelable.callback();
    callback();
    EXPECT_EQ(1, count);
  }

  callback();
  EXPECT_EQ(1, count);
}

TEST(CancelableCallbackTest, IsCanceled) {
  CancelableClosure cancelable;
  EXPECT_TRUE(cancelable.IsCanceled());

  int count = 0;
  cancelable.Reset([&count] { count++; });
  EXPECT_FALSE(cancelable.IsCanceled());

  cancelable.Cancel();
  EXPECT_TRUE(cancelable.IsCanceled());
}

TEST(CancelableCallbackTest, Reset) {
  int count = 0;
  CancelableClosure cancelable([&count] { count++; });

  auto callback = cancelable.callback();
  callback();
  EXPECT_EQ(1, count);

  callback();
  EXPECT_EQ(2, count);

  cancelable.Reset([&count] { count += 3; });
  EXPECT_FALSE(cancelable.IsCanceled());

  // The stale copy of the cancelable callback is non-null.
  ASSERT_TRUE(callback);

  // The stale copy of the cancelable callback is no longer active.
  callback();
  EXPECT_EQ(2, count);

  auto callback2 = cancelable.callback();
  ASSERT_TRUE(callback2);

  callback2();
  EXPECT_EQ(5, count);
}

TEST(CancelableCallbackTest, VaryingSignatures) {
  int count = 0;

  // Single-argument function via std::bind
  CancelableCallback<void(int)> cancelable(
      std::bind(&AddBToA, &count, std::placeholders::_1));

  auto callback = cancelable.callback();
  callback(2);
  EXPECT_EQ(2, count);

  cancelable.Cancel();
  callback(2);
  EXPECT_EQ(2, count);

  // Single-argument lambda
  cancelable.Reset([&count](int b) { count += b; });
  callback = cancelable.callback();
  callback(3);
  EXPECT_EQ(5, count);
  cancelable.Cancel();
  callback(3);
  EXPECT_EQ(5, count);

  // Two-argument lambda
  CancelableCallback<void(int*, int)> cancelable2(
      [](int* a, int b) { (*a) += b; });
  auto callback2 = cancelable2.callback();
  callback2(&count, 2);
  EXPECT_EQ(7, count);
  cancelable2.Cancel();
  callback2(&count, 2);
  EXPECT_EQ(7, count);

  // Two-argument function pointer
  cancelable2.Reset(AddBToA);
  callback2 = cancelable2.callback();
  callback2(&count, 3);
  EXPECT_EQ(10, count);
  cancelable2.Cancel();
  callback2(&count, 3);
  EXPECT_EQ(10, count);
}

}  // namespace
}  // namespace fxl
