// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/target/module.h"

#include <random>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/framework/testing/module.h"

namespace fuzzing {
namespace {

// Unit tests.

TEST(ModuleTest, Identifier) {
  // Prepare a fixed module.
  std::vector<Module::PC> pc_table1;
  for (size_t i = 0; i < FakeModule::kNumPCs; ++i) {
    pc_table1.emplace_back(Module::PC{0x1000 + i * 0x10, i % 8});
  }
  FakeModule module1(std::move(pc_table1));
  Identifier expected = {10350636416433154085ULL, 15439583565181265701ULL};
  EXPECT_EQ(module1.id(), expected);

  // Shifting all the PCs by a random basis does not affect the source ID, i.e., the ID is
  // independent of where it is mapped in memory.
  std::vector<Module::PC> pc_table2;
  for (size_t i = 0; i < FakeModule::kNumPCs; ++i) {
    pc_table2.emplace_back(Module::PC{0xdeadbeef + i * 0x10, i % 8});
  }
  FakeModule module2(std::move(pc_table2));
  EXPECT_EQ(module1.id(), module2.id());

  // Changing the counters has no effect on identifiers.
  memset(module1.counters(), 1, module1.num_pcs());
  EXPECT_EQ(module1.id(), expected);

  // Check for collisions. This isn't exhaustive; it is simply a smoke test to check if things are
  // very broken.
  for (uint32_t i = 0; i < 100; ++i) {
    FakeModule moduleN(/* seed */ i);
    EXPECT_NE(moduleN.id(), expected);
  }
}

TEST(ModuleTest, UpdateAndClear) {
  FakeModule module;
  std::minstd_rand prng(1);

  // Initial contents are shared.
  for (size_t i = 0; i < module.num_pcs(); ++i) {
    module[i] = static_cast<uint8_t>(prng());
  }
  std::vector<uint8_t> expected(module.counters(), module.counters_end());

  SharedMemory shmem;
  shmem.LinkMirrored(module.Share());
  auto* data = shmem.data();

  module.Update();
  std::vector<uint8_t> actual(data, data + module.num_pcs());
  EXPECT_EQ(actual, expected);

  // Changes to counters are not reflected until an |Update|.
  for (size_t i = 0; i < module.num_pcs(); ++i) {
    module[i] = static_cast<uint8_t>(prng());
  }
  actual = std::vector<uint8_t>(data, data + module.num_pcs());
  EXPECT_EQ(actual, expected);

  module.Update();
  expected = std::vector<uint8_t>(module.counters(), module.counters_end());
  actual = std::vector<uint8_t>(data, data + module.num_pcs());
  EXPECT_EQ(actual, expected);

  // Clearing resets counters to zero (but does not |Update|, for performance reasons).
  module.Clear();
  actual = std::vector<uint8_t>(data, data + module.num_pcs());
  EXPECT_EQ(actual, expected);

  expected = std::vector<uint8_t>(module.num_pcs(), 0);
  actual = std::vector<uint8_t>(module.counters(), module.counters_end());
  EXPECT_EQ(actual, expected);
}

}  // namespace
}  // namespace fuzzing
