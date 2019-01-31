// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arch/x86/cpuid.h>

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

struct TestDataSet {
    Features::Feature features[200];
    Features::Feature missing_features[200];
    Registers leaf0;
    Registers leaf1;
    Registers leaf4;
    Registers leaf7;
    Registers leafB[3];
    Registers leaf80000001;
};

// Queried from a Intel Xeon E5-2690v4.
const TestDataSet Xeon2690v4Data = {
    .features = {Features::FPU, Features::VME, Features::DE, Features::PSE, Features::TSC,
                 Features::MSR, Features::PAE, Features::MCE, Features::CX8, Features::APIC, Features::SEP,
                 Features::MTRR, Features::PGE, Features::MCA, Features::CMOV, Features::PAT,
                 Features::PSE36, Features::ACPI, Features::MMX, Features::FSGSBASE,
                 Features::FXSR, Features::SSE, Features::SSE2, Features::SS, Features::HTT, Features::TM,
                 Features::PBE, Features::SYSCALL, Features::XD, Features::PDPE1GB, Features::RDTSCP,
                 Features::PCLMULQDQ, Features::DTES64, Features::MONITOR, Features::DS_CPL, Features::VMX,
                 Features::SMX, Features::EST, Features::TM2, Features::SSSE3, Features::SDBG,
                 Features::FMA, Features::CX16, Features::XTPR, Features::PDCM, Features::PCID,
                 Features::DCA, Features::SSE4_1, Features::SSE4_2, Features::X2APIC, Features::MOVBE,
                 Features::POPCNT, Features::AES, Features::XSAVE, Features::AVX, Features::F16C,
                 Features::RDRAND, Features::LAHF, Features::BMI1, Features::HLE, Features::AVX2,
                 Features::SMEP, Features::BMI2, Features::ERMS, Features::INVPCID, Features::RTM,
                 Features::RDSEED, Features::ADX, Features::SMAP, Features::INTEL_PT},
    .missing_features = {Features::PSN, Features::AVX512VNNI},
    .leaf0 = {.reg = {0x14, 0x756E6547, 0x6C65746E, 0x49656E69}},
    .leaf1 = {.reg = {0x406F1, 0x12200800, 0x7FFEFBFF, 0xBFEBFBFF}},
    .leaf4 = {.reg = {0x3C004121, 0x1C0003F, 0x3F, 0x0}},
    .leaf7 = {.reg = {0x0, 0x21CBFBB, 0x0, 0x9C000000}},
    .leafB = {{.reg = {0x1, 0x2, 0x100, 0x28}},
              {.reg = {0x5, 0x1C, 0x201, 0x29}},
              {.reg = {0x0, 0x0, 0x2, 0x38}}},
    .leaf80000001 = {.reg = {0x0, 0x0, 0x121, 0x2C100800}},
};

bool test_feature_flags() {
    BEGIN_TEST;

    auto data = Xeon2690v4Data;
    Features features(data.leaf1, data.leaf7, data.leaf80000001);

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

    auto data = Xeon2690v4Data;
    Features features(data.leaf1, data.leaf7, data.leaf80000001);

    EXPECT_EQ(32, features.max_logical_processors_in_package(), "");

    END_TEST;
}

bool test_manufacturer_info() {
    BEGIN_TEST;
    auto data = Xeon2690v4Data;

    char buffer[ManufacturerInfo::kManufacturerIdLength + 1] = {0};
    auto info = ManufacturerInfo(data.leaf0);
    info.manufacturer_id(buffer);

    EXPECT_TRUE(strcmp("GenuineIntel", buffer) == 0, buffer);
    EXPECT_EQ(ManufacturerInfo::INTEL, info.manufacturer(), "");
    EXPECT_EQ(20u, info.highest_cpuid_leaf(), "");

    END_TEST;
}

bool test_processor_id() {
    BEGIN_TEST;
    auto data = Xeon2690v4Data;

    ProcessorId proc(data.leaf1);
    EXPECT_EQ(6, proc.family(), "");
    EXPECT_EQ(79, proc.model(), "");
    EXPECT_EQ(1, proc.stepping(), "");

    END_TEST;
}

bool test_topology() {
    BEGIN_TEST;
    auto data = Xeon2690v4Data;

    Topology topology(ManufacturerInfo(data.leaf0),
                      Features(data.leaf1, data.leaf7, data.leaf80000001),
                      data.leaf4, data.leafB);

    const auto levels_opt = topology.levels();
    ASSERT_TRUE(levels_opt, "");

    const auto& levels = *levels_opt;
    EXPECT_EQ(Topology::LevelType::SMT, levels.levels[0].type, "");
    EXPECT_EQ(2u, levels.levels[0].node_count, "");
    EXPECT_EQ(1u, levels.levels[0].shift_width, "");

    EXPECT_EQ(Topology::LevelType::CORE, levels.levels[1].type, "");
    EXPECT_EQ(14u, levels.levels[1].node_count, "");
    EXPECT_EQ(4u, levels.levels[1].shift_width, "");

    EXPECT_EQ(Topology::LevelType::INVALID, levels.levels[2].type, "");

    END_TEST;
}

// Tests other intel path, using leaf4 instead of extended leafB.
bool test_topology_intel_leaf4() {
    BEGIN_TEST;
    auto data = Xeon2690v4Data;

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
    EXPECT_EQ(1u, levels.levels[0].shift_width, "");

    EXPECT_EQ(Topology::LevelType::CORE, levels.levels[1].type, "");
    EXPECT_EQ(Topology::kInvalidCount, levels.levels[1].node_count, "");
    EXPECT_EQ(4u, levels.levels[1].shift_width, "");

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
