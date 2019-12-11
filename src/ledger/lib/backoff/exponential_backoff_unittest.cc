// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/backoff/exponential_backoff.h"

#include <stdlib.h>

#include "gtest/gtest.h"

namespace ledger {

constexpr zx::duration kDefaultInitialValue = zx::msec(10);

uint64_t GetSeed() { return 1u; }

class ExponentialBackoffTest : public ::testing::Test {
 public:
  ExponentialBackoffTest() {}
  ExponentialBackoffTest(const ExponentialBackoffTest&) = delete;
  ExponentialBackoffTest& operator=(const ExponentialBackoffTest&) = delete;
  ~ExponentialBackoffTest() override {}
};

TEST_F(ExponentialBackoffTest, GrowExponentionally) {
  ExponentialBackoff backoff(kDefaultInitialValue, 2, zx::duration::infinite(), GetSeed);

  int factor = 1;
  for (size_t i = 0; i < 5u; i++) {
    zx::duration delay = backoff.GetNext();
    EXPECT_GE(delay, kDefaultInitialValue * factor);
    EXPECT_LE(delay, kDefaultInitialValue * factor * 2);
    factor *= 2;
  }
}

TEST_F(ExponentialBackoffTest, Reset) {
  ExponentialBackoff backoff(kDefaultInitialValue, 2, zx::duration::infinite(), GetSeed);

  for (size_t i = 0; i < 4; ++i) {
    zx::duration delay = backoff.GetNext();
    EXPECT_GE(delay, kDefaultInitialValue);
    EXPECT_LT(delay, kDefaultInitialValue * 2);
    backoff.Reset();
  }
}

TEST_F(ExponentialBackoffTest, NoOverflow) {
  ExponentialBackoff backoff(kDefaultInitialValue, 2, zx::duration::infinite(), GetSeed);

  zx::duration previous = backoff.GetNext();
  for (size_t i = 0; i < 200u; i++) {
    zx::duration next = backoff.GetNext();
    EXPECT_GE(next, previous);
    previous = next;
  }
}

TEST_F(ExponentialBackoffTest, MaxDelay) {
  constexpr zx::duration kMaxDelay = zx::sec(20);

  ExponentialBackoff backoff(kDefaultInitialValue, 2, kMaxDelay, GetSeed);

  for (size_t i = 0; i < 64; i++) {
    zx::duration delay = backoff.GetNext();
    EXPECT_GE(delay, kDefaultInitialValue);
    EXPECT_LE(delay, kMaxDelay);
  }

  EXPECT_EQ(backoff.GetNext(), kMaxDelay);
}

TEST_F(ExponentialBackoffTest, Random) {
  ExponentialBackoff backoff1(GetSeed);
  ExponentialBackoff backoff2([] { return 2u; });

  EXPECT_NE(backoff1.GetNext(), backoff2.GetNext());
}

}  // namespace ledger
