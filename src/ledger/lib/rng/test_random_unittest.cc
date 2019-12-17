// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/rng/test_random.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace ledger {
namespace {

using ::testing::ElementsAreArray;
using ::testing::Not;

TEST(TestRandomTest, InUniquePtr) {
  auto test_random = std::make_unique<TestRandom>(0);
  std::unique_ptr<Random> random = std::move(test_random);
  EXPECT_TRUE(random);
}

TEST(TestRandomTest, DependantDraws) {
  constexpr size_t kNbElement = 20;

  TestRandom random1(0);
  TestRandom random2(0);

  std::vector<uint8_t> v1(kNbElement, 0);
  std::vector<uint8_t> v2(kNbElement, 0);

  random1.Draw(&v1);
  random2.Draw(&v2);

  EXPECT_THAT(v1, ElementsAreArray(v2));
}

TEST(TestRandomTest, ConsequentDraws) {
  constexpr size_t kNbElement = 20;

  TestRandom random1(0);

  std::vector<uint8_t> v1(kNbElement, 0);
  std::vector<uint8_t> v2(kNbElement, 0);

  random1.Draw(&v1);
  random1.Draw(&v2);

  EXPECT_THAT(v1, Not(ElementsAreArray(v2)));
}

TEST(TestRandomTest, IndependantDraws) {
  constexpr size_t kNbElement = 20;

  TestRandom random1(0);
  TestRandom random2(1);

  std::vector<uint8_t> v1(kNbElement, 0);
  std::vector<uint8_t> v2(kNbElement, 0);

  random1.Draw(&v1);
  random2.Draw(&v2);

  EXPECT_THAT(v1, Not(ElementsAreArray(v2)));
}

TEST(TestRandomTest, NoSeedTruncation) {
  // Tests that the seed is not accidentally truncated when initializing the
  // generator.
  constexpr size_t kNbElement = 20;

  TestRandom random1(1);
  TestRandom random2(1 + (1 << 8));
  TestRandom random3(1 + (1 << 16));
  TestRandom random4(1 + (uint64_t(1) << 32));

  std::vector<uint8_t> v1(kNbElement, 0);
  std::vector<uint8_t> v2(kNbElement, 0);
  std::vector<uint8_t> v3(kNbElement, 0);
  std::vector<uint8_t> v4(kNbElement, 0);

  random1.Draw(&v1);
  random2.Draw(&v2);
  random3.Draw(&v3);
  random4.Draw(&v4);

  EXPECT_THAT(v1, Not(ElementsAreArray(v2)));
  EXPECT_THAT(v1, Not(ElementsAreArray(v3)));
  EXPECT_THAT(v1, Not(ElementsAreArray(v4)));
}

}  // namespace
}  // namespace ledger
