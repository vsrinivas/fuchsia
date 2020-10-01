// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/third_party/enum_utils/enum_utils.h"

#include <gtest/gtest.h>

#include "src/ui/lib/escher/util/enum_utils.h"

namespace {
using namespace escher;

enum class EnumForCycling { kZero = 0, kOne, kTwo, kThree, kEnumCount };

TEST(EnumCycle, NextAndPrevious) {
  EXPECT_EQ(EnumForCycling::kThree, EnumCycle(EnumForCycling::kTwo));
  EXPECT_EQ(EnumForCycling::kOne, EnumCycle(EnumForCycling::kTwo, true));
}

TEST(EnumCycle, Wraparound) {
  EXPECT_EQ(EnumForCycling::kZero, EnumCycle(EnumForCycling::kThree));
  EXPECT_EQ(EnumForCycling::kThree, EnumCycle(EnumForCycling::kZero, true));
}

TEST(EnumArray, Correctness) {
  std::array<EnumForCycling, 4> array = EnumArray<EnumForCycling>();
  EXPECT_EQ(array[0], EnumForCycling::kZero);
  EXPECT_EQ(array[1], EnumForCycling::kOne);
  EXPECT_EQ(array[2], EnumForCycling::kTwo);
  EXPECT_EQ(array[3], EnumForCycling::kThree);
}

enum class EnumForCountingValues {
  // Order should not matter.
  kMinusTen = -10,
  kTen = 10,
  kMinusOne = -1,
  kZero = 0,
  kOne,
};

TEST(EnumElements, Count) {
  auto count = enum_utils::CountEnumElement<EnumForCountingValues>();
  EXPECT_EQ(count, 5u);

  // Setting |Min| arguments.
  count = enum_utils::CountEnumElement<EnumForCountingValues, 0>();
  // Only kZero, kOne and kTen are counted.
  EXPECT_EQ(count, 3u);

  // Setting |Max| arguments.
  count = enum_utils::CountEnumElement<EnumForCountingValues, 0, 10>();
  // Only kZero and kOne are counted.
  EXPECT_EQ(count, 2u);
}

TEST(EnumElements, Maximum) {
  auto max_element = *enum_utils::MaxEnumElementValue<EnumForCountingValues>();
  EXPECT_EQ(max_element, 10);

  // Setting |Min| arguments.
  max_element = *enum_utils::MaxEnumElementValue<EnumForCountingValues, 0>();
  // Only kZero, kOne and kTen are counted.
  EXPECT_EQ(max_element, 10);

  // Setting |Max| arguments.
  max_element = *enum_utils::MaxEnumElementValue<EnumForCountingValues, -10, 0>();
  // Only kMinusTen and kMinusOne are counted.
  EXPECT_EQ(max_element, -1);
}

TEST(EnumElements, Minimum) {
  auto min_element = *enum_utils::MinEnumElementValue<EnumForCountingValues>();
  EXPECT_EQ(min_element, -10);

  // Setting |Min| arguments.
  min_element = *enum_utils::MinEnumElementValue<EnumForCountingValues, 0>();
  // Only kZero, kOne and kTen are counted.
  EXPECT_EQ(min_element, 0);

  // Setting |Max| arguments.
  min_element = *enum_utils::MinEnumElementValue<EnumForCountingValues, -10, 0>();
  // Only kMinusTen and kMinusOne are counted.
  EXPECT_EQ(min_element, -10);
}

}  // namespace
