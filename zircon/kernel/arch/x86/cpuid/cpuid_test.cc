// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/unittest/unittest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <initializer_list>

#include <arch/x86/cpuid.h>
#include <arch/x86/cpuid_test_data.h>

namespace {
using cpu_id::CpuId;
using cpu_id::Features;
using cpu_id::ManufacturerInfo;
using cpu_id::ProcessorId;
using cpu_id::Registers;
using cpu_id::Topology;

}  // namespace

bool test_intel_feature_flags() {
  BEGIN_TEST;

  for (const auto& data : {cpu_id::kTestDataCorei5_6260U, cpu_id::kTestDataXeon2690v4,
                           cpu_id::kTestDataCeleronJ3455}) {
    cpu_id::FakeCpuId fake_cpu(data);
    auto features = fake_cpu.ReadFeatures();

    // Features we know this processor has.
    for (auto feature : data.features) {
      if (feature.leaf == Features::INVALID_SET)
        continue;

      const bool result = features.HasFeature(feature);
      if (!result) {
        printf("Missing Feature: set:%u reg:%u bit:%u\n", feature.leaf, feature.reg, feature.bit);
      }
      EXPECT_TRUE(result);
    }

    // Some features we know it doesn't.
    for (auto feature : data.missing_features) {
      if (feature.leaf == Features::INVALID_SET)
        continue;

      const bool result = features.HasFeature(feature);
      if (result) {
        printf("Extra Feature: set:%u reg:%u bit:%u\n", feature.leaf, feature.reg, feature.bit);
      }
      EXPECT_FALSE(result);
    }
  }

  END_TEST;
}

bool test_amd_feature_flags() {
  BEGIN_TEST;

  for (const auto& data : {cpu_id::kTestDataThreadRipper2970wx, cpu_id::kTestDataAmdA49120C}) {
    cpu_id::FakeCpuId fake_cpu(data);
    auto features = fake_cpu.ReadFeatures();

    // Features we know this processor has.
    for (auto feature : data.features) {
      if (feature.leaf == Features::INVALID_SET)
        continue;

      const bool result = features.HasFeature(feature);
      if (!result) {
        printf("Missing Feature: set:%u reg:%u bit:%u\n", feature.leaf, feature.reg, feature.bit);
      }
      EXPECT_TRUE(result);
    }

    // Some features we know it doesn't.
    for (auto feature : data.missing_features) {
      if (feature.leaf == Features::INVALID_SET)
        continue;

      const bool result = features.HasFeature(feature);
      if (result) {
        printf("Extra Feature: set:%u reg:%u bit:%u\n", feature.leaf, feature.reg, feature.bit);
      }
      EXPECT_FALSE(result);
    }
  }

  END_TEST;
}

bool test_intel_max_logical_processors() {
  BEGIN_TEST;

  auto features = cpu_id::kCpuIdXeon2690v4.ReadFeatures();

  EXPECT_EQ(32, features.max_logical_processors_in_package());

  END_TEST;
}

bool test_amd_max_logical_processors() {
  BEGIN_TEST;

  auto features = cpu_id::kCpuIdThreadRipper2970wx.ReadFeatures();

  EXPECT_EQ(48, features.max_logical_processors_in_package());

  END_TEST;
}
bool test_intel_manufacturer_info() {
  BEGIN_TEST;

  char buffer[ManufacturerInfo::kManufacturerIdLength + 1] = {0};
  auto info = cpu_id::kCpuIdXeon2690v4.ReadManufacturerInfo();
  info.manufacturer_id(buffer);

  EXPECT_TRUE(strcmp("GenuineIntel", buffer) == 0, buffer);
  EXPECT_EQ(ManufacturerInfo::INTEL, info.manufacturer());
  EXPECT_EQ(20u, info.highest_cpuid_leaf());
  EXPECT_EQ(0x80000008u, info.highest_extended_cpuid_leaf());

  END_TEST;
}

bool test_amd_manufacturer_info() {
  BEGIN_TEST;

  char buffer[ManufacturerInfo::kManufacturerIdLength + 1] = {0};
  auto info = cpu_id::kCpuIdThreadRipper2970wx.ReadManufacturerInfo();
  info.manufacturer_id(buffer);

  EXPECT_TRUE(strcmp("AuthenticAMD", buffer) == 0, buffer);
  EXPECT_EQ(ManufacturerInfo::AMD, info.manufacturer());
  EXPECT_EQ(13u, info.highest_cpuid_leaf());
  EXPECT_EQ(0x8000001Fu, info.highest_extended_cpuid_leaf());

  END_TEST;
}

bool test_eve_processor_id() {
  BEGIN_TEST;

  // Pulled from a Pixelbook 'Google Eve rev8'
  Registers regs;
  regs.reg[cpu_id::Registers::EAX] = 0x806e9u;
  cpu_id::ProcessorId id(regs);

  EXPECT_EQ(id.signature(), 0x806e9u);
  EXPECT_EQ(id.family(), 6u);
  EXPECT_EQ(id.model(), 0x8eu);
  EXPECT_EQ(id.stepping(), 9u);

  END_TEST;
}

bool test_intel_processor_id() {
  BEGIN_TEST;

  auto proc = cpu_id::kCpuIdXeon2690v4.ReadProcessorId();
  EXPECT_EQ(6, proc.family());
  EXPECT_EQ(79, proc.model());
  EXPECT_EQ(1, proc.stepping());

  END_TEST;
}

bool test_amd_processor_id() {
  BEGIN_TEST;

  auto proc = cpu_id::kCpuIdThreadRipper2970wx.ReadProcessorId();
  EXPECT_EQ(0x17, proc.family());
  EXPECT_EQ(0x8, proc.model());
  EXPECT_EQ(0x2, proc.stepping());

  END_TEST;
}

bool test_intel_topology() {
  BEGIN_TEST;
  auto topology = cpu_id::kCpuIdXeon2690v4.ReadTopology();

  const auto levels_opt = topology.levels();
  ASSERT_TRUE(levels_opt);

  const auto& levels = *levels_opt;
  EXPECT_EQ(Topology::LevelType::SMT, levels.levels[0].type);
  EXPECT_EQ(2u, levels.levels[0].node_count);
  EXPECT_EQ(1u, levels.levels[0].id_bits);

  EXPECT_EQ(Topology::LevelType::CORE, levels.levels[1].type);
  EXPECT_EQ(14u, levels.levels[1].node_count);
  EXPECT_EQ(4u, levels.levels[1].id_bits);

  EXPECT_EQ(Topology::LevelType::INVALID, levels.levels[2].type);

  END_TEST;
}

// Tests other intel path, using leaf4 instead of extended leafB.
bool test_intel_topology_leaf4() {
  BEGIN_TEST;
  auto data = cpu_id::kTestDataXeon2690v4;

  // We need to report that we don't support leafB.
  auto modifiedLeaf0 = data.leaf0;
  modifiedLeaf0.reg[Registers::EAX] = 4;
  auto manufacturer = ManufacturerInfo(modifiedLeaf0, data.leaf8_0);
  EXPECT_EQ(4u, manufacturer.highest_cpuid_leaf());

  Topology topology(manufacturer,
                    Features(data.leaf1, data.leaf6, data.leaf7, data.leaf8_1, data.leaf8_8),
                    data.leaf4, data.leafB, data.leaf8_8, data.leaf8_1D, data.leaf8_1E);

  const auto levels_opt = topology.levels();
  ASSERT_TRUE(levels_opt);

  const auto& levels = *levels_opt;
  ASSERT_EQ(Topology::LevelType::SMT, levels.levels[0].type);
  EXPECT_EQ(Topology::kInvalidCount, levels.levels[0].node_count);
  EXPECT_EQ(1u, levels.levels[0].id_bits);

  ASSERT_EQ(Topology::LevelType::CORE, levels.levels[1].type);
  EXPECT_EQ(Topology::kInvalidCount, levels.levels[1].node_count);
  EXPECT_EQ(4u, levels.levels[1].id_bits);

  EXPECT_EQ(Topology::LevelType::INVALID, levels.levels[2].type);

  END_TEST;
}

bool test_amd_topology() {
  BEGIN_TEST;
  auto topology = cpu_id::kCpuIdThreadRipper2970wx.ReadTopology();

  const auto levels_opt = topology.levels();
  ASSERT_TRUE(levels_opt);

  const auto& levels = *levels_opt;
  ASSERT_EQ(Topology::LevelType::SMT, levels.levels[0].type);
  EXPECT_EQ(1u, levels.levels[0].id_bits);

  ASSERT_EQ(Topology::LevelType::CORE, levels.levels[1].type);
  EXPECT_EQ(3u, levels.levels[1].id_bits);

  ASSERT_EQ(Topology::LevelType::DIE, levels.levels[2].type);
  EXPECT_EQ(2u, levels.levels[2].id_bits);

  END_TEST;
}

bool test_intel_highest_cache() {
  BEGIN_TEST;
  auto topology = cpu_id::kCpuIdXeon2690v4.ReadTopology();

  const auto cache = topology.highest_level_cache();

  EXPECT_EQ(3u, cache.level);
  EXPECT_EQ(5u, cache.shift_width);
  EXPECT_EQ(35u << 20 /*35 megabytes*/, cache.size_bytes);

  END_TEST;
}

bool test_amd_highest_cache() {
  BEGIN_TEST;
  auto topology = cpu_id::kCpuIdThreadRipper2970wx.ReadTopology();

  const auto cache = topology.highest_level_cache();

  EXPECT_EQ(3u, cache.level);
  EXPECT_EQ(3u, cache.shift_width);
  EXPECT_EQ(8u << 20 /*8 megabytes*/, cache.size_bytes);

  END_TEST;
}

UNITTEST_START_TESTCASE(cpuid_tests)
UNITTEST("Parse feature flags from static Intel data.", test_intel_feature_flags)
UNITTEST("Parse feature flags from static AMD data.", test_amd_feature_flags)
UNITTEST("Parse maximum logical processors from Intel static data.",
         test_intel_max_logical_processors)
UNITTEST("Parse maximum logical processors from AMD static data.", test_amd_max_logical_processors)
UNITTEST("Parse manufacturer info from Intel static data.", test_intel_manufacturer_info)
UNITTEST("Parse manufacturer info from AMD static data.", test_amd_manufacturer_info)
UNITTEST("Parse processor id from Intel static data.", test_intel_processor_id)
UNITTEST("Parse processor id from AMD static data.", test_amd_processor_id)
UNITTEST("Parse topology from static Intel data.", test_intel_topology)
UNITTEST("Parse topology from static data, using leaf4.", test_intel_topology_leaf4)
UNITTEST("Parse topology from static AMD data.", test_amd_topology)
UNITTEST("Parse cache information from static Intel data.", test_intel_highest_cache)
UNITTEST("Parse cache information from static AMD data.", test_amd_highest_cache)
UNITTEST_END_TESTCASE(cpuid_tests, "cpuid", "Test parsing of cpuid values.")
