// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/rng/test_random.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace rng {
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

}  // namespace
}  // namespace rng
