// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915/poll-until.h"

#include <gtest/gtest.h>

namespace i915 {

namespace {

// A predicate whose value changes and tracks how many times it's been invoked.
class PredicateCounter {
 public:
  explicit PredicateCounter(int threshold) : threshold_(threshold) {}

  bool increment_and_compare() {
    ++counter_;
    return counter_ >= threshold_;
  }

  int counter() const { return counter_; }

 private:
  int counter_ = 0;
  int threshold_;
};

TEST(PollUntilTest, TrueOnFirstPoll) {
  PredicateCounter always_true(0);

  const bool poll_result =
      PollUntil([&] { return always_true.increment_and_compare(); }, zx::nsec(1), 10);
  EXPECT_EQ(true, poll_result);
  EXPECT_EQ(1, always_true.counter());
}

TEST(PollUntilTest, TrueAfterTwoPolls) {
  PredicateCounter always_true(2);

  const bool poll_result =
      PollUntil([&] { return always_true.increment_and_compare(); }, zx::nsec(1), 10);
  EXPECT_EQ(true, poll_result);
  EXPECT_EQ(2, always_true.counter());
}

TEST(PollUntilTest, TrueAfterMaximuPolls) {
  PredicateCounter always_true(10);

  const bool poll_result =
      PollUntil([&] { return always_true.increment_and_compare(); }, zx::nsec(1), 10);
  EXPECT_EQ(true, poll_result);
  EXPECT_EQ(10, always_true.counter());
}

TEST(PollUntilTest, Timeout) {
  PredicateCounter always_true(100);

  const bool poll_result =
      PollUntil([&] { return always_true.increment_and_compare(); }, zx::nsec(1), 10);
  EXPECT_EQ(false, poll_result);
  EXPECT_EQ(11, always_true.counter());
}

}  // namespace

}  // namespace i915
