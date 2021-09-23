// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/module-pool.h"

#include <gtest/gtest.h>

#include "src/sys/fuzzing/framework/engine/module-proxy.h"

namespace fuzzing {
namespace {

TEST(ModulePoolTest, Empty) {
  ModulePool pool;
  EXPECT_EQ(pool.Measure(), 0U);
  EXPECT_EQ(pool.Accumulate(), 0U);
  pool.Clear();
}

TEST(ModulePoolTest, Get) {
  ModulePool pool;

  // Creates missing proxies automatically.
  auto* proxy1 = pool.Get({0, 0}, sizeof(uint64_t));
  EXPECT_NE(proxy1, nullptr);

  // Different IDs result in different proxies.
  auto* proxy2 = pool.Get({0, 1}, sizeof(uint64_t));
  EXPECT_NE(proxy1, proxy2);

  // Different sizes result in different proxies, mitigating hash collisions further.
  auto* proxy3 = pool.Get({0, 0}, sizeof(uint64_t) * 2);
  EXPECT_NE(proxy1, proxy3);

  // Can return previously created proxies.
  auto* proxy4 = pool.Get({0, 0}, sizeof(uint64_t));
  EXPECT_EQ(proxy1, proxy4);
}

TEST(ModulePoolTest, ForEachModule) {
  ModulePool pool;

  // Set up two proxies with two attached "modules" each.
  uint64_t counters1a[1] = {0x01};
  auto* proxy1 = pool.Get({0, 0}, sizeof(counters1a));
  proxy1->Add(counters1a, sizeof(counters1a));

  uint64_t counters1b[1] = {0x02};
  proxy1->Add(counters1b, sizeof(counters1b));

  uint64_t counters2a[2] = {0x03, 0x04};
  auto* proxy2 = pool.Get({0, 0}, sizeof(counters2a));
  proxy2->Add(counters2a, sizeof(counters2a));

  uint64_t counters2b[2] = {0x05, 0x06};
  proxy2->Add(counters2b, sizeof(counters2b));

  // Check that the pool aggregates correctly.
  size_t num_features;
  EXPECT_EQ(pool.Measure(), 3U);
  EXPECT_EQ(pool.Accumulate(), 3U);
  EXPECT_EQ(pool.GetCoverage(&num_features), 3U);
  EXPECT_EQ(num_features, 3U);

  EXPECT_EQ(pool.Measure(), 0U);
  EXPECT_EQ(pool.Accumulate(), 0U);
  EXPECT_EQ(pool.GetCoverage(&num_features), 3U);
  EXPECT_EQ(num_features, 3U);

  pool.Clear();
  EXPECT_EQ(pool.Measure(), 3U);
  EXPECT_EQ(pool.Accumulate(), 3U);
  EXPECT_EQ(pool.GetCoverage(&num_features), 3U);
  EXPECT_EQ(num_features, 3U);

  // Add new counters to two of the "modules". Note the second should NOT result in a new feature,
  // since 13 (0x08 + 0x05) is in the same "bucket" as the previous value of 8 (0x03 + 0x05).
  counters1a[0] = 0x07;
  counters2a[0] = 0x08;
  EXPECT_EQ(pool.Measure(), 1U);
  EXPECT_EQ(pool.Accumulate(), 1U);
  EXPECT_EQ(pool.GetCoverage(&num_features), 3U);
  EXPECT_EQ(num_features, 4U);
}

}  // namespace
}  // namespace fuzzing
