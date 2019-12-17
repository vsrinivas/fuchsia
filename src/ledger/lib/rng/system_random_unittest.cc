// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/rng/system_random.h"

#include <algorithm>
#include <random>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace ledger {
namespace {

using ::testing::ElementsAre;
using ::testing::Ge;
using ::testing::Le;
using ::testing::SizeIs;

TEST(TestRandomTest, InUniquePtr) {
  auto test_random = std::make_unique<SystemRandom>();
  std::unique_ptr<Random> random = std::move(test_random);
  EXPECT_TRUE(random);
}

TEST(SystemRandomTest, MiscRandomTest) {
  constexpr size_t kNbElement = 20;

  SystemRandom random;

  std::vector<uint32_t> v1;
  for (size_t i = 0; i < kNbElement; ++i) {
    v1.push_back(random.Draw<uint32_t>());
  }
  EXPECT_TRUE(std::any_of(v1.begin(), v1.end(), [](const uint8_t& v) { return v != 0; }));

  std::vector<uint8_t> v2(kNbElement, 0);
  random.Draw(&v2);
  EXPECT_TRUE(std::any_of(v2.begin(), v2.end(), [](const uint8_t& v) { return v != 0; }));

  uint8_t bytes[kNbElement];
  random.Draw(bytes, kNbElement);
  EXPECT_TRUE(std::any_of(bytes, bytes + kNbElement, [](const uint8_t& v) { return v != 0; }));
}

TEST(SystemRandomTest, BitGeneratorTest) {
  constexpr size_t kNbElement = 100;
  SystemRandom random;

  auto engine = random.NewBitGenerator<uint32_t>();
  std::discrete_distribution<> d({1, 1, 1, 1});
  std::set<uint32_t> s;
  for (size_t i = 0; i < kNbElement; ++i) {
    s.insert(d(engine));
  }
  EXPECT_THAT(s, ElementsAre(0, 1, 2, 3));
}

TEST(SystemRandomTest, RandomStructTest) {
  constexpr size_t kNbElement = 64;
  struct S {
    uint64_t array[kNbElement];
  };
  SystemRandom random;

  auto s1 = random.Draw<S>();
  std::set<uint64_t> v1(s1.array, s1.array + kNbElement);

  EXPECT_THAT(v1, SizeIs(Ge(kNbElement - 2)));

  auto s2 = random.Draw<S>();
  std::set<uint64_t> v2(s2.array, s2.array + kNbElement);

  std::vector<uint64_t> common_data;
  set_intersection(v1.begin(), v1.end(), v2.begin(), v2.end(), std::back_inserter(common_data));
  EXPECT_THAT(common_data, SizeIs(Le(2u)));
}

TEST(SystemRandomTest, IndependantDraws) {
  constexpr size_t kNbElement = 20;

  SystemRandom random1;
  SystemRandom random2;

  std::vector<uint8_t> v1(kNbElement, 0);
  std::vector<uint8_t> v2(kNbElement, 0);

  random1.Draw(&v1);
  random2.Draw(&v2);

  EXPECT_NE(v1, v2);
}

}  // namespace
}  // namespace ledger
