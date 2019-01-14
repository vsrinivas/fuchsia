// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arch/x86/cpuid.h>

#include <arch/x86/cpuid_test_data.h>
#include <initializer_list>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lib/unittest/unittest.h>

namespace {
using cpu_id::CpuId;
using cpu_id::Features;
using cpu_id::ManufacturerInfo;
using cpu_id::ProcessorId;
using cpu_id::Registers;
using cpu_id::Topology;

} // namespace

bool test_feature_flags() {
    BEGIN_TEST;

    auto& data = cpu_id::kTestDataXeon2690v4;
    auto features = cpu_id::kCpuIdXeon2690v4.ReadFeatures();

    // Features we know this processor has.
    for (auto feature : data.features) {
        if (feature.leaf == Features::INVALID_SET)
            continue;

        const bool result = features.HasFeature(feature);
        if (!result) {
            printf("Missing Feature: set:%u reg:%u bit:%u\n",
                   feature.leaf, feature.reg, feature.bit);
        }
        EXPECT_TRUE(result, "");
    }

    // Some features we know it doesn't.
    for (auto feature : data.missing_features) {
        if (feature.leaf == Features::INVALID_SET)
            continue;

        const bool result = features.HasFeature(feature);
        if (result) {
            printf("Extra Feature: set:%u reg:%u bit:%u\n",
                   feature.leaf, feature.reg, feature.bit);
        }
        EXPECT_FALSE(result, "");
    }

    END_TEST;
}

bool test_max_logical_processors() {
    BEGIN_TEST;

    auto features = cpu_id::kCpuIdXeon2690v4.ReadFeatures();

    EXPECT_EQ(32, features.max_logical_processors_in_package(), "");

    END_TEST;
}

bool test_manufacturer_info() {
    BEGIN_TEST;

    char buffer[ManufacturerInfo::kManufacturerIdLength + 1] = {0};
    auto info = cpu_id::kCpuIdXeon2690v4.ReadManufacturerInfo();
    info.manufacturer_id(buffer);

    EXPECT_TRUE(strcmp("GenuineIntel", buffer) == 0, buffer);
    EXPECT_EQ(ManufacturerInfo::INTEL, info.manufacturer(), "");
    EXPECT_EQ(20u, info.highest_cpuid_leaf(), "");

    END_TEST;
}

bool test_processor_id() {
    BEGIN_TEST;

    auto proc = cpu_id::kCpuIdXeon2690v4.ReadProcessorId();
    EXPECT_EQ(6, proc.family(), "");
    EXPECT_EQ(79, proc.model(), "");
    EXPECT_EQ(1, proc.stepping(), "");

    END_TEST;
}

bool test_topology() {
    BEGIN_TEST;
    auto topology = cpu_id::kCpuIdXeon2690v4.ReadTopology();

    const auto levels_opt = topology.levels();
    ASSERT_TRUE(levels_opt, "");

    const auto& levels = *levels_opt;
    EXPECT_EQ(Topology::LevelType::SMT, levels.levels[0].type, "");
    EXPECT_EQ(2u, levels.levels[0].node_count, "");
    EXPECT_EQ(1u, levels.levels[0].id_bits, "");

    EXPECT_EQ(Topology::LevelType::CORE, levels.levels[1].type, "");
    EXPECT_EQ(14u, levels.levels[1].node_count, "");
    EXPECT_EQ(4u, levels.levels[1].id_bits, "");

    EXPECT_EQ(Topology::LevelType::INVALID, levels.levels[2].type, "");

    END_TEST;
}

// Tests other intel path, using leaf4 instead of extended leafB.
bool test_topology_intel_leaf4() {
    BEGIN_TEST;
    auto data = cpu_id::kTestDataXeon2690v4;

    // We need to report that we don't support leafB.
    auto modifiedLeaf0 = data.leaf0;
    modifiedLeaf0.reg[Registers::EAX] = 4;
    auto manufacturer = ManufacturerInfo(modifiedLeaf0);
    EXPECT_EQ(4u, manufacturer.highest_cpuid_leaf(), "");

    Topology topology(manufacturer,
                      Features(data.leaf1, data.leaf7, data.leaf80000001),
                      data.leaf4, data.leafB);

    const auto levels_opt = topology.levels();
    ASSERT_TRUE(levels_opt, "");

    const auto& levels = *levels_opt;
    EXPECT_EQ(Topology::LevelType::SMT, levels.levels[0].type, "");
    EXPECT_EQ(Topology::kInvalidCount, levels.levels[0].node_count, "");
    EXPECT_EQ(1u, levels.levels[0].id_bits, "");

    EXPECT_EQ(Topology::LevelType::CORE, levels.levels[1].type, "");
    EXPECT_EQ(Topology::kInvalidCount, levels.levels[1].node_count, "");
    EXPECT_EQ(4u, levels.levels[1].id_bits, "");

    EXPECT_EQ(Topology::LevelType::INVALID, levels.levels[2].type, "");

    END_TEST;
}

UNITTEST_START_TESTCASE(cpuid_tests)
UNITTEST("Parse feature flags from static data.", test_feature_flags)
UNITTEST("Parse maximum logical processors from static data.", test_max_logical_processors)
UNITTEST("Parse manufacturer info from static data.", test_manufacturer_info)
UNITTEST("Parse processor id from static data.", test_processor_id)
UNITTEST("Parse topology from static data.", test_topology)
UNITTEST("Parse topology from static data, using leaf4.", test_topology_intel_leaf4)
UNITTEST_END_TESTCASE(cpuid_tests, "cpuid", "Test parsing of cpuid values.");
