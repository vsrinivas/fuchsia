// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

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

}  // namespace

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
UNITTEST("Parse processor id from Eve static data.", test_eve_processor_id)
UNITTEST_END_TESTCASE(cpuid_tests, "cpuid", "Test parsing of cpuid values.")
