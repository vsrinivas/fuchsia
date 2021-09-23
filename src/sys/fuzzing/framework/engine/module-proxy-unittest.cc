// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/module-proxy.h"

#include <random>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/shared-memory.h"
#include "src/sys/fuzzing/framework/target/module.h"
#include "src/sys/fuzzing/framework/testing/module.h"

namespace fuzzing {
namespace {

// Test fixture.

TEST(ModuleProxyTest, Measure) {
  ModuleProxy proxy({0, 0}, FakeModule::kNumPCs);

  // No modules added.
  EXPECT_EQ(proxy.Measure(), 0U);

  // Add a module.
  FakeModule module0;
  proxy.Add(module0.counters(), module0.num_pcs());
  module0[0] = 1;
  module0[1] = 1;
  module0[2] = 1;
  EXPECT_EQ(proxy.Measure(), 3U);

  // Idempotent.
  EXPECT_EQ(proxy.Measure(), 3U);

  // Same counters, different features.
  FakeModule module1;
  proxy.Add(module1.counters(), module1.num_pcs());
  module1[0] = 2;
  module1[1] = 2;
  module1[2] = 2;
  EXPECT_EQ(proxy.Measure(), 3U);

  // Different counters, different features.
  FakeModule module2;
  proxy.Add(module2.counters(), module2.num_pcs());
  module2[3] = 4;
  module2[4] = 4;
  module2[5] = 4;
  EXPECT_EQ(proxy.Measure(), 6U);

  // All the bits.
  memset(module0.counters(), 0xff, module0.num_pcs());
  memset(module1.counters(), 0xff, module1.num_pcs());
  memset(module2.counters(), 0xff, module2.num_pcs());
  EXPECT_EQ(proxy.Measure(), FakeModule::kNumPCs);
}

TEST(ModuleProxyTest, Accumulate) {
  ModuleProxy proxy({0, 0}, FakeModule::kNumPCs);

  // No modules added.
  EXPECT_EQ(proxy.Accumulate(), 0U);

  // Add a module.
  FakeModule module0;
  proxy.Add(module0.counters(), module0.num_pcs());
  module0[0] = 1;
  module0[1] = 1;
  module0[2] = 1;
  EXPECT_EQ(proxy.Accumulate(), 3U);

  // Features are no longer "new".
  EXPECT_EQ(proxy.Accumulate(), 0U);

  // Same counters, but different features.
  FakeModule module1;
  proxy.Add(module1.counters(), module1.num_pcs());
  module1[0] = 2;
  module1[1] = 2;
  module1[2] = 2;
  EXPECT_EQ(proxy.Accumulate(), 3U);

  // Different counters and different features.
  FakeModule module2;
  proxy.Add(module2.counters(), module2.num_pcs());
  module2[3] = 4;
  module2[4] = 4;
  module2[5] = 4;
  EXPECT_EQ(proxy.Accumulate(), 3U);

  // Clear accumulated.
  proxy.Clear();
  EXPECT_EQ(proxy.Accumulate(), 6U);

  // All the bits.
  memset(module0.counters(), 0xff, module0.num_pcs());
  memset(module1.counters(), 0xff, module1.num_pcs());
  memset(module2.counters(), 0xff, module2.num_pcs());
  EXPECT_EQ(proxy.Accumulate(), FakeModule::kNumPCs);
}

TEST(ModuleProxyTest, GetCoverage) {
  ModuleProxy proxy({0, 0}, FakeModule::kNumPCs);

  // No coverage until a call to |Accumulate|.
  FakeModule module0;
  proxy.Add(module0.counters(), module0.num_pcs());
  module0[0] = 1;
  module0[1] = 1;
  module0[2] = 1;

  size_t num_features;
  EXPECT_EQ(proxy.GetCoverage(&num_features), 0U);
  EXPECT_EQ(num_features, 0U);

  EXPECT_EQ(proxy.Accumulate(), 3U);
  EXPECT_EQ(proxy.GetCoverage(&num_features), 3U);
  EXPECT_EQ(num_features, 3U);

  // Idempotent.
  EXPECT_EQ(proxy.GetCoverage(&num_features), 3U);
  EXPECT_EQ(num_features, 3U);

  // More features, but same number of PCs.
  while (module0[0]) {
    module0[0] += 1;
    module0[1] += 1;
    module0[2] += 1;
    EXPECT_LE(proxy.Accumulate(), 3U);
  }
  EXPECT_EQ(proxy.GetCoverage(&num_features), 3U);
  EXPECT_EQ(num_features, 24U);
}

TEST(ModuleProxyTest, Remove) {
  ModuleProxy proxy({0, 0}, FakeModule::kNumPCs);

  // No modules added.
  EXPECT_EQ(proxy.Accumulate(), 0U);

  // Add a module.
  FakeModule module0;
  proxy.Add(module0.counters(), module0.num_pcs());
  module0[0] = 1;
  module0[1] = 1;
  module0[2] = 1;
  EXPECT_EQ(proxy.Accumulate(), 3U);

  FakeModule module1;
  proxy.Add(module1.counters(), module1.num_pcs());
  module1[0] = 2;
  module1[1] = 2;
  module1[2] = 2;
  EXPECT_EQ(proxy.Accumulate(), 3U);

  // Remove counters. The counter sums reduce, leading to new features.
  proxy.Remove(module0.counters());
  EXPECT_EQ(proxy.Accumulate(), 3U);

  // Removed counters have no effect.
  module0[1] = 10;
  EXPECT_EQ(proxy.Accumulate(), 0U);

  // Removal doesn't affect accumulated.
  module1[1] = 1;
  EXPECT_EQ(proxy.Accumulate(), 0U);
}

TEST(ModuleProxyTest, Features) {
  uint8_t counters[sizeof(uint64_t)];
  memset(counters, 0, sizeof(counters));
  ModuleProxy proxy({0, 0}, sizeof(counters));
  proxy.Add(counters, sizeof(counters));

  // Every (non-zero) counter value maps to one feature.
  EXPECT_EQ(proxy.Measure(), 0U);
  for (size_t i = 1; i < 256; ++i) {
    counters[0] = static_cast<uint8_t>(i);
    EXPECT_EQ(proxy.Measure(), 1U);
  }

  // Measure and Accumulate detect exactly the same new features.
  for (size_t i = 0; i < 256; ++i) {
    counters[0] = static_cast<uint8_t>(i);
    EXPECT_EQ(proxy.Measure(), proxy.Accumulate());
  }

  // The inline 8-bit counter can map to 8 possible features.
  proxy.Clear();
  size_t total = 0;
  for (size_t i = 0; i < 256; ++i) {
    counters[0] = static_cast<uint8_t>(i);
    total += proxy.Accumulate();
  }
  EXPECT_EQ(total, 8U);
}

TEST(ModuleProxyTest, FromModule) {
  FakeModule fake;
  auto module = std::make_unique<Module>(fake.counters(), fake.pcs(), fake.num_pcs());

  // Share it.
  auto shmem = std::make_unique<SharedMemory>();
  shmem->LinkMirrored(module->Share());

  // Add module to a proxy.
  ModuleProxy proxy(module->id(), shmem->size());
  proxy.Add(shmem->data(), shmem->size());
  EXPECT_EQ(proxy.Measure(), 0U);

  // Update a counter and propagate.
  fake[fake.num_pcs() - 1] = 255;
  module->Update();
  EXPECT_EQ(proxy.Measure(), 1U);

  // Remove the module from the proxy.
  proxy.Remove(shmem->data());

  // Can discard objects once removed.
  module.reset();
  shmem.reset();
  EXPECT_EQ(proxy.Measure(), 0U);
}

}  // namespace
}  // namespace fuzzing
