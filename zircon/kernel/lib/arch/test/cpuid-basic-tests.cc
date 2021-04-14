// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/testing/x86/fake-cpuid.h>
#include <lib/arch/x86/cpuid.h>

#include <iomanip>

#include <gtest/gtest.h>

namespace {

TEST(CpuidTests, Family) {
  {
    auto version =
        arch::CpuidVersionInfo::Get().FromValue(0).set_base_family(0xf).set_extended_family(0xf0);
    EXPECT_EQ(0xff, version.family());
  }

  // Extended family ID is ignored for other families.
  {
    auto version = arch::CpuidVersionInfo::Get()
                       .FromValue(0)
                       .set_base_family(0x6)
                       // Suppose this is garbage or an internal detail.
                       .set_extended_family(0xf0);
    EXPECT_EQ(0x06, version.family());
  }
}

TEST(CpuidTests, Model) {
  {
    auto version = arch::CpuidVersionInfo::Get()
                       .FromValue(0)
                       .set_base_family(0x6)
                       .set_base_model(0xa)
                       .set_extended_model(0xb);
    EXPECT_EQ(0xba, version.model());
  }

  {
    auto version = arch::CpuidVersionInfo::Get()
                       .FromValue(0)
                       .set_base_family(0xf)
                       .set_base_model(0xa)
                       .set_extended_model(0xb);
    EXPECT_EQ(0xba, version.model());
  }

  // Extended model ID is ignored for other families.
  {
    auto version = arch::CpuidVersionInfo::Get()
                       .FromValue(0)
                       .set_base_family(0x1)
                       .set_base_model(0xa)
                       // Suppose this is garbage or an internal detail.
                       .set_extended_model(0xf);
    EXPECT_EQ(0x0a, version.model());
  }
}

TEST(CpuidTests, GetMicroarchitectureFromVersion) {
  // Particular SoCs judiciously picked at random.
  struct {
    arch::Vendor vendor;
    uint8_t extended_family;
    uint8_t base_family;
    uint8_t extended_model;
    uint8_t base_model;
    arch::Microarchitecture expected;
  } test_cases[] = {
      // An unknown vendor should result in an unknown microarchitecture.
      {arch::Vendor::kUnknown, 0xa, 0xb, 0xc, 0xd, arch::Microarchitecture::kUnknown},
      // Intel Clarksfield.
      {arch::Vendor::kIntel, 0x0, 0x6, 0x1, 0xe, arch::Microarchitecture::kIntelNehalem},
      // Intel Coffee Lake S.
      {arch::Vendor::kIntel, 0x0, 0x6, 0x9, 0xe, arch::Microarchitecture::kIntelSkylake},
      // Intel Skylake SP
      {arch::Vendor::kIntel, 0x0, 0x6, 0x5, 0x5, arch::Microarchitecture::kIntelSkylakeServer},
      // Intel Tangier.
      {arch::Vendor::kIntel, 0x0, 0x6, 0x4, 0xa, arch::Microarchitecture::kIntelSilvermont},
      // AMD Kaveri.
      {arch::Vendor::kAmd, 0x6, 0xf, 0x3, 0x0, arch::Microarchitecture::kAmdFamilyBulldozer},
      // AMD Banded Kestrel.
      {arch::Vendor::kAmd, 0x8, 0xf, 0x1, 0x8, arch::Microarchitecture::kAmdFamilyZen},
  };

  for (const auto& test_case : test_cases) {
    auto version = arch::CpuidVersionInfo::Get()
                       .FromValue(0)
                       .set_extended_family(test_case.extended_family)
                       .set_base_family(test_case.base_family)
                       .set_extended_model(test_case.extended_model)
                       .set_base_model(test_case.base_model);
    auto actual = version.microarchitecture(test_case.vendor);
    std::string_view actual_sv = arch::ToString(actual);
    std::string_view expected_sv = arch::ToString(test_case.expected);
    std::string_view vendor_sv = arch::ToString(test_case.vendor);

    EXPECT_EQ(test_case.expected, actual)
        << "expected a microarchictecture of " << std::quoted(expected_sv)
        << " for (vendor, extended family, base family, extended model, base model) = "
        << "(" << vendor_sv << ", " << std::hex << test_case.extended_family << ", " << std::hex
        << test_case.base_family << ", " << std::hex << test_case.extended_model << ", " << std::hex
        << test_case.base_model << "); got " << std::quoted(actual_sv);
  }
}

TEST(CpuidTests, CpuidSupports) {
  using arch::testing::X86Microprocessor;

  {
    // Max basic leaf: 0xa;
    // Max hypervisor leaf: 0x0;
    // Max extended leaf: 0x8000'0008.
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelAtom330);

    // Supported basic leaf (0xa).
    EXPECT_TRUE(arch::CpuidSupports<arch::CpuidPerformanceMonitoringA>(cpuid));

    // Unsupported basic leaf (0x14).
    EXPECT_FALSE(arch::CpuidSupports<arch::CpuidProcessorTraceMainB>(cpuid));

    // Unsupported hypervisor leaf (0x4000'0000)
    EXPECT_FALSE(arch::CpuidSupports<arch::CpuidMaximumHypervisorLeaf>(cpuid));

    // Supported extended leaf (0x8000'0008).
    EXPECT_TRUE(arch::CpuidSupports<arch::CpuidExtendedAmdFeatureFlagsB>(cpuid));

    // Unsupported extended leaf (0x8000'001e).
    EXPECT_FALSE(arch::CpuidSupports<arch::CpuidExtendedApicId>(cpuid));
  }

  {
    // Max basic leaf: 0x10;
    // Max hypervisor leaf: 0x0;
    // Max extended leaf: 0x8000'0020.
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen9_3950x);

    // Supported 0x8000'001d, 0x8000'001e (has topology extensions).
    using CpuidAmdCacheTopologyA_0 = arch::CpuidAmdCacheTopologyA<0>;
    EXPECT_TRUE(arch::CpuidSupports<CpuidAmdCacheTopologyA_0>(cpuid));
    EXPECT_TRUE(arch::CpuidSupports<arch::CpuidExtendedApicId>(cpuid));
  }

  {
    // Max basic leaf: 0xd;
    // Max hypervisor leaf: 0x4000'0010;
    // Max extended leaf: 0x8000'001e.
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen9_3950xVmware);

    // Supported hypervisor leaf (0x4000'0000).
    EXPECT_TRUE(arch::CpuidSupports<arch::CpuidMaximumHypervisorLeaf>(cpuid));

    // Unsupported 0x8000'001d, 0x8000'001e (no topology extensions).
    using CpuidAmdCacheTopologyA_0 = arch::CpuidAmdCacheTopologyA<0>;
    EXPECT_FALSE(arch::CpuidSupports<CpuidAmdCacheTopologyA_0>(cpuid));
    EXPECT_FALSE(arch::CpuidSupports<arch::CpuidExtendedApicId>(cpuid));
  }
}

}  // namespace
