// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/testing/x86/fake-cpuid.h>
#include <lib/arch/x86/cpuid.h>

#include <zxtest/zxtest.h>

// This file is meant for tests that exercise basic CPUID lib/arch logic that
// does not rely upon CpuidIo access (but rather the business logic around it),
// Tests that deal in CpuidIo access should written in cpuid-corpus-tests.cc
// and leverage the corpus of CPUID values for expectations around particular
// processors.

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
      {arch::Vendor::kAmd, 0x6, 0xf, 0x3, 0x0, arch::Microarchitecture::kAmdFamily0x15},
      // AMD Banded Kestrel.
      {arch::Vendor::kAmd, 0x8, 0xf, 0x1, 0x8, arch::Microarchitecture::kAmdFamily0x17},
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

    EXPECT_EQ(test_case.expected, actual,
              "\nexpected a microarchictecture of \"%.*s\" for "
              "(vendor, extended family, base family, extended model, base model) = "
              "(%.*s, %#x, %#x, %#x, %#x); got \"%.*s\"",
              static_cast<int>(expected_sv.size()), expected_sv.data(),
              static_cast<int>(vendor_sv.size()), vendor_sv.data(), test_case.extended_family,
              test_case.base_family, test_case.extended_model, test_case.base_model,
              static_cast<int>(actual_sv.size()), actual_sv.data());
  }
}

}  // namespace
