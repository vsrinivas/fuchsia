// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/testing/x86/fake-cpuid.h>
#include <lib/arch/testing/x86/fake-msr.h>
#include <lib/arch/x86/msr.h>
#include <lib/arch/x86/speculation.h>

#include <gtest/gtest.h>

namespace {

using arch::testing::X86Microprocessor;

// Tweaks the CPUID values so that IA32_ARCH_CAPABILITIES (bit 29 of EDX of
// leaf 0x7) reports as present.
void MakeArchCapabilitiesAvailable(arch::testing::FakeCpuidIo& cpuid) {
  auto& leaf7 = cpuid.Get<arch::CpuidExtendedFeatureFlagsD>()->values_;
  cpuid.Populate(arch::CpuidExtendedFeatureFlagsD::kLeaf,
                 arch::CpuidExtendedFeatureFlagsD::kSubleaf, leaf7[arch::CpuidIo::kEax],
                 leaf7[arch::CpuidIo::kEbx], leaf7[arch::CpuidIo::kEcx],
                 leaf7[arch::CpuidIo::kEdx] | (uint32_t{1} << 29));
}

TEST(SpeculationTests, HasIbpb) {
  // Intel Core 2 6300.
  // Does not have IBPB.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCore2_6300);
    EXPECT_FALSE(arch::HasIbpb(cpuid));
  }

  // Intel Xeon E5-2690 v4.
  // Has IBPB.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelXeonE5_2690_V4);
    EXPECT_TRUE(arch::HasIbpb(cpuid));
  }

  // AMD Ryzen 5 1500X.
  // Does not have IBPB.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen5_1500x);
    EXPECT_FALSE(arch::HasIbpb(cpuid));
  }

  // AMD Ryzen 9 3950X.
  // Has IBPB.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen9_3950x);
    EXPECT_TRUE(arch::HasIbpb(cpuid));
  }
}

TEST(SpeculationTests, HasIbrs) {
  // Intel Core 2 6300.
  // Does not have IBRS.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCore2_6300);
    arch::testing::FakeMsrIo msr;
    EXPECT_FALSE(arch::HasIbrs(cpuid, msr, /*always_on_mode=*/false));
    EXPECT_FALSE(arch::HasIbrs(cpuid, msr, /*always_on_mode=*/true));
  }

  // Intel Xeon E5-2690 v4.
  // Has IBRS; does not have an always-on mode.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelXeonE5_2690_V4);
    arch::testing::FakeMsrIo msr;
    EXPECT_TRUE(arch::HasIbrs(cpuid, msr, /*always_on_mode=*/false));
    EXPECT_FALSE(arch::HasIbrs(cpuid, msr, /*always_on_mode=*/true));

    // Suppose we perform a microcode update that enables the always-on
    // mode...
    MakeArchCapabilitiesAvailable(cpuid);
    msr.Populate(arch::X86Msr::IA32_ARCH_CAPABILITIES, 0b10);  // IBRS_ALL.
    EXPECT_TRUE(arch::HasIbrs(cpuid, msr, /*always_on_mode=*/false));
    EXPECT_TRUE(arch::HasIbrs(cpuid, msr, /*always_on_mode=*/true));
  }

  // AMD Ryzen 5 1500X.
  // Does not have IBRS.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen5_1500x);
    arch::testing::FakeMsrIo msr;
    EXPECT_FALSE(arch::HasIbrs(cpuid, msr, /*always_on_mode=*/false));
    EXPECT_FALSE(arch::HasIbrs(cpuid, msr, /*always_on_mode=*/true));
  }
}

TEST(SpeculationTests, HasStibp) {
  // Intel Core 2 6300.
  // Does not have STIBP.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCore2_6300);
    EXPECT_FALSE(arch::HasStibp(cpuid, /*always_on_mode=*/false));
    EXPECT_FALSE(arch::HasStibp(cpuid, /*always_on_mode=*/true));
  }

  // Intel Xeon E5-2690 v4.
  // Has STIBP; does not have an always-on mode (like all Intel hardware).
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelXeonE5_2690_V4);
    EXPECT_TRUE(arch::HasStibp(cpuid, /*always_on_mode=*/false));
    EXPECT_FALSE(arch::HasStibp(cpuid, /*always_on_mode=*/true));
  }

  // AMD Ryzen 5 1500X.
  // Does not have STIBP.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen5_1500x);
    EXPECT_FALSE(arch::HasStibp(cpuid, /*always_on_mode=*/false));
    EXPECT_FALSE(arch::HasStibp(cpuid, /*always_on_mode=*/true));
  }

  // AMD Ryzen 9 3950X.
  // Has STIBP; does not have an always-on mode.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen9_3950x);
    EXPECT_TRUE(arch::HasStibp(cpuid, /*always_on_mode=*/false));
    EXPECT_FALSE(arch::HasStibp(cpuid, /*always_on_mode=*/true));

    // Suppose we perform a microcode update that enables the always-on
    // mode...
    uint32_t features = cpuid.Read<arch::CpuidExtendedAmdFeatureFlagsB>().reg_value() |
                        uint32_t{1} << 17;  // STIBP_ALWAYS_ON
    cpuid.Populate(arch::CpuidExtendedAmdFeatureFlagsB::kLeaf, 0, arch::CpuidIo::kEbx, features);
    EXPECT_TRUE(arch::HasStibp(cpuid, /*always_on_mode=*/false));
    EXPECT_TRUE(arch::HasStibp(cpuid, /*always_on_mode=*/true));
  }
}

}  // namespace
