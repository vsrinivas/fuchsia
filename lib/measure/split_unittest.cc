// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/measure/split.h"

#include "gtest/gtest.h"

namespace tracing {
namespace measure {

bool operator==(const SampleRange& rhs, const SampleRange& lhs) {
  return rhs.samples == lhs.samples && rhs.begin == lhs.begin &&
         rhs.end == lhs.end;
}

namespace {

TEST(Split, NoSamples) {
  std::vector<SampleRange> result = Split({}, {0u, 2u});
  std::vector<SampleRange> expected;
  EXPECT_EQ(expected, result);
}

TEST(Split, NoSplit) {
  std::vector<SampleRange> result = Split({1u, 2u, 3u}, {});
  std::vector<SampleRange> expected = {{{1u, 2u, 3u}, 0u, 3u}};
  EXPECT_EQ(expected, result);
}

TEST(Split, YesSplit) {
  std::vector<SampleRange> result = Split({41u, 42u, 43u, 44u}, {1u, 2u});
  std::vector<SampleRange> expected = {
      {{41u}, 0u, 1u}, {{42u}, 1u, 2u}, {{43u, 44u}, 2u, 4u}};
  EXPECT_EQ(expected, result);
}

}  // namespace

}  // namespace measure
}  // namespace tracing
