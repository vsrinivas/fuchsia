// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <lib/unittest/unittest.h>
#include <zircon/types.h>

#include <arch/x86/cpuid.h>
#include <arch/x86/cpuid_test_data.h>
#include <arch/x86/fake_msr_access.h>
#include <arch/x86/hwp.h>
#include <ktl/optional.h>

namespace x86 {
namespace {

uint64_t MakeHwpRequest(uint8_t min_perf, uint8_t max_perf, uint8_t desired_perf, uint8_t epp) {
  return static_cast<uint64_t>(min_perf) | (static_cast<uint64_t>(max_perf) << 8ull) |
         (static_cast<uint64_t>(desired_perf) << 16ull) | (static_cast<uint64_t>(epp) << 24ull);
}

bool TestParsePolicy() {
  BEGIN_TEST;

  // Valid parse.
  auto parsed_policy = IntelHwpParsePolicy("bios-specified");
  ASSERT_TRUE(parsed_policy.has_value());
  EXPECT_EQ(parsed_policy.value(), IntelHwpPolicy::kBiosSpecified);

  // Invalid parses.
  EXPECT_TRUE(!IntelHwpParsePolicy(nullptr).has_value());
  EXPECT_TRUE(!IntelHwpParsePolicy("").has_value());
  EXPECT_TRUE(!IntelHwpParsePolicy("invalid").has_value());
  EXPECT_TRUE(!IntelHwpParsePolicy("\n").has_value());

  END_TEST;
}

bool TestIntelHwpSupported() {
  BEGIN_TEST;

  // AMD processors that don't support Intel HWP.
  ASSERT_FALSE(IntelHwpSupported(&cpu_id::kCpuIdAmdA49120C));
  ASSERT_FALSE(IntelHwpSupported(&cpu_id::kCpuIdThreadRipper2970wx));

  // Intel processors supporting HWP.
  ASSERT_TRUE(IntelHwpSupported(&cpu_id::kCpuIdCorei5_6260U));

  // Older Intel processors not supporting HWP.
  ASSERT_FALSE(IntelHwpSupported(&cpu_id::kCpuIdXeon2690v4));
  ASSERT_FALSE(IntelHwpSupported(&cpu_id::kCpuIdCeleronJ3455));

  END_TEST;
}

bool TestNoCpuSupport() {
  BEGIN_TEST;

  // HWP_PREF not supported, expect no MSR writes.
  FakeMsrAccess fake_msrs = {};
  IntelHwpInit(&cpu_id::kCpuIdXeon2690v4, &fake_msrs, IntelHwpPolicy::kBalanced);
  // An empty FakeMsrAccess will panic if you attempt to write to any uninitialized MSRs.

  END_TEST;
}

static bool TestUseBiosValues() {
  BEGIN_TEST;

  // Skylake-U has HWP_PREF and EPB
  FakeMsrAccess fake_msrs = {};
  fake_msrs.msrs_[0] = {X86_MSR_IA32_ENERGY_PERF_BIAS, 0x5};
  fake_msrs.msrs_[1] = {X86_MSR_IA32_PM_ENABLE, 0x0};
  // min = 0x11, max=0xfe, efficient=0x22, guaranteed=0x33.
  fake_msrs.msrs_[2] = {X86_MSR_IA32_HWP_CAPABILITIES, 0x11'22'33'FEull};
  fake_msrs.msrs_[3] = {X86_MSR_IA32_HWP_REQUEST, 0x0ull};

  IntelHwpInit(&cpu_id::kCpuIdCorei5_6260U, &fake_msrs, IntelHwpPolicy::kBiosSpecified);

  // Ensure HWP was enabled.
  EXPECT_EQ(fake_msrs.read_msr(X86_MSR_IA32_PM_ENABLE), 1u);  // HWP enabled.

  // Check the generated HWP request is correct.
  //
  // We expect IA32_ENERGY_PERF_BIAS = 0x5 mapped to 0x40 EPP, min/max copied
  // from HWP capabilities, and desired set to 0 (auto).
  EXPECT_EQ(
      fake_msrs.read_msr(X86_MSR_IA32_HWP_REQUEST),
      MakeHwpRequest(/*min_perf=*/0x11, /*max_perf=*/0xfe, /*desired_perf=*/0x0, /*epp=*/0x40));

  END_TEST;
}

static bool TestUsePerformancePolicy() {
  BEGIN_TEST;

  FakeMsrAccess fake_msrs = {};
  fake_msrs.msrs_[1] = {X86_MSR_IA32_PM_ENABLE, 0x0};
  // min = 0x11, max=0xfe, guaranteed=0x33, efficient=0x22
  fake_msrs.msrs_[2] = {X86_MSR_IA32_HWP_CAPABILITIES, 0x11'22'33'FEull};
  fake_msrs.msrs_[3] = {X86_MSR_IA32_HWP_REQUEST, 0x0ull};

  // Skylake-U has HWP_PREF.
  IntelHwpInit(&cpu_id::kCpuIdCorei5_6260U, &fake_msrs, IntelHwpPolicy::kPerformance);

  // Ensure HWP was enabled.
  EXPECT_EQ(fake_msrs.read_msr(X86_MSR_IA32_PM_ENABLE), 1u);  // HWP enabled.

  // Check the generated HWP request is correct.
  //
  // We expect perf preference set to max performance (0x0), min/max copied
  // from the HWP capabilitiy, and desired set to 0 (auto).
  EXPECT_EQ(
      fake_msrs.read_msr(X86_MSR_IA32_HWP_REQUEST),
      MakeHwpRequest(/*min_perf=*/0x11, /*max_perf=*/0xfe, /*desired_perf=*/0x0, /*epp=*/0x0));

  END_TEST;
}

static bool TestUseStablePerformancePolicy() {
  BEGIN_TEST;

  FakeMsrAccess fake_msrs = {};
  fake_msrs.msrs_[1] = {X86_MSR_IA32_PM_ENABLE, 0x0};
  // min = 0x11, max=0xfe, guaranteed=0x33, efficient=0x22
  fake_msrs.msrs_[2] = {X86_MSR_IA32_HWP_CAPABILITIES, 0x11'22'33'FEull};
  fake_msrs.msrs_[3] = {X86_MSR_IA32_HWP_REQUEST, 0x0ull};

  // Skylake-U has HWP_PREF.
  IntelHwpInit(&cpu_id::kCpuIdCorei5_6260U, &fake_msrs, IntelHwpPolicy::kStablePerformance);

  // Ensure HWP was enabled.
  EXPECT_EQ(fake_msrs.read_msr(X86_MSR_IA32_PM_ENABLE), 1u);  // HWP enabled.

  // Check the generated HWP request is correct.
  //
  // We expect perf preference set to max performance (0x0), min/max/desired
  // all set to the guaranteed performance value.
  EXPECT_EQ(
      fake_msrs.read_msr(X86_MSR_IA32_HWP_REQUEST),
      MakeHwpRequest(/*min_perf=*/0x33, /*max_perf=*/0x33, /*desired_perf=*/0x33, /*epp=*/0x0));

  END_TEST;
}

}  // namespace
}  // namespace x86

UNITTEST_START_TESTCASE(x86_hwp_tests)
UNITTEST("TestParsePolicy", x86::TestParsePolicy)
UNITTEST("TestIntelHwpSupported", x86::TestIntelHwpSupported)
UNITTEST("TestNoCpuSupport", x86::TestNoCpuSupport)
UNITTEST("TestUseBiosValues", x86::TestUseBiosValues)
UNITTEST("TestPerformancePolicy", x86::TestUsePerformancePolicy)
UNITTEST("TestStablePerformancePolicy", x86::TestUseStablePerformancePolicy)
UNITTEST_END_TESTCASE(x86_hwp_tests, "x86_hwp", "x86 Intel HWP tests")
