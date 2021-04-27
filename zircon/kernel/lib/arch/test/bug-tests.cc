// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/testing/x86/fake-cpuid.h>
#include <lib/arch/testing/x86/fake-msr.h>
#include <lib/arch/x86/bug.h>
#include <lib/arch/x86/msr.h>

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

TEST(BugTests, Swapgs) {
  // We generally expect only Intel CPUs to be affected, of which the majority
  // should be able mitigate (`lfence` has been around for a long time).
  {
    const arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelAtom330);
    EXPECT_TRUE(arch::HasX86SwapgsBug(cpuid));
  }
  {
    const arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCoreI7_6700k);
    EXPECT_TRUE(arch::HasX86SwapgsBug(cpuid));
  }
  {
    const arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdA10_7870k);
    EXPECT_FALSE(arch::HasX86SwapgsBug(cpuid));
  }
  {
    const arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen9_3950x);
    EXPECT_FALSE(arch::HasX86SwapgsBug(cpuid));
  }
}

TEST(BugTests, MdsTaa) {
  // Intel Xeon E5-2690 V4 (Broadwell).
  {
    // Older microcode: No IA32_ARCH_CAPABILITIES.
    // Expectations: MDS and TAA.
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelXeonE5_2690_V4);
    arch::testing::FakeMsrIo msr;

    // Has MD_CLEAR at this point.
    EXPECT_TRUE(arch::CanMitigateX86MdsTaaBugs(cpuid));

    EXPECT_TRUE(arch::HasX86MdsBugs(cpuid, msr));
    EXPECT_TRUE(arch::HasX86TaaBug(cpuid, msr));
    EXPECT_TRUE(arch::HasX86MdsTaaBugs(cpuid, msr));

    // Newer microcode: IA32_ARCH_CAPABILITIES, but no mitigation.
    // Expectations: MDS and TAA.
    MakeArchCapabilitiesAvailable(cpuid);
    msr.Populate(arch::X86Msr::IA32_ARCH_CAPABILITIES, 0);

    EXPECT_TRUE(arch::HasX86MdsBugs(cpuid, msr));
    EXPECT_TRUE(arch::HasX86TaaBug(cpuid, msr));
    EXPECT_TRUE(arch::HasX86MdsTaaBugs(cpuid, msr));

    // Newer microcode: IA32_ARCH_CAPABILITIES, MDS mitigation.
    // Expectations: no MDS and TAA.
    msr.Populate(arch::X86Msr::IA32_ARCH_CAPABILITIES, uint32_t{1} << 5);

    EXPECT_FALSE(arch::HasX86MdsBugs(cpuid, msr));
    EXPECT_TRUE(arch::HasX86TaaBug(cpuid, msr));
    EXPECT_TRUE(arch::HasX86MdsTaaBugs(cpuid, msr));

    // Newer microcode: IA32_ARCH_CAPABILITIES, MDS and TAA mitigation.
    // Expectations: no MDS and no TAA.
    msr.Populate(arch::X86Msr::IA32_ARCH_CAPABILITIES, uint32_t{1} << 5 | uint32_t{1} << 8);
    EXPECT_FALSE(arch::HasX86MdsBugs(cpuid, msr));
    EXPECT_FALSE(arch::HasX86TaaBug(cpuid, msr));
    EXPECT_FALSE(arch::HasX86MdsTaaBugs(cpuid, msr));
  }

  // Intel Atom x5-Z8350 (Airmont)
  {
    // Older microcode: No IA32_ARCH_CAPABILITIES.
    // Expectations: MDS and no TAA.
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelAtomX5_Z8350);
    arch::testing::FakeMsrIo msr;

    // Does not have MD_CLEAR at this point.
    EXPECT_FALSE(arch::CanMitigateX86MdsTaaBugs(cpuid));

    EXPECT_TRUE(arch::HasX86MdsBugs(cpuid, msr));
    EXPECT_FALSE(arch::HasX86TaaBug(cpuid, msr));
    EXPECT_TRUE(arch::HasX86MdsTaaBugs(cpuid, msr));

    // Newer microcode: IA32_ARCH_CAPABILITIES, but no MDS mitigation.
    // Expectations: MDS and no TAA.
    MakeArchCapabilitiesAvailable(cpuid);
    msr.Populate(arch::X86Msr::IA32_ARCH_CAPABILITIES, 0);

    EXPECT_TRUE(arch::HasX86MdsBugs(cpuid, msr));
    EXPECT_FALSE(arch::HasX86TaaBug(cpuid, msr));
    EXPECT_TRUE(arch::HasX86MdsTaaBugs(cpuid, msr));

    // Newer microcode: IA32_ARCH_CAPABILITIES, MDS mitigation.
    // Expectations: no MDS and no TAA.
    msr.Populate(arch::X86Msr::IA32_ARCH_CAPABILITIES, uint32_t{1} << 5);

    EXPECT_FALSE(arch::HasX86MdsBugs(cpuid, msr));
    EXPECT_FALSE(arch::HasX86TaaBug(cpuid, msr));
    EXPECT_FALSE(arch::HasX86MdsTaaBugs(cpuid, msr));
  }

  // AMD Ryzen 5 1500X.
  // Expectations: no MDS and no TAA.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen5_1500x);
    arch::testing::FakeMsrIo msr;
    EXPECT_FALSE(arch::HasX86MdsBugs(cpuid, msr));
    EXPECT_FALSE(arch::HasX86TaaBug(cpuid, msr));
    EXPECT_FALSE(arch::HasX86MdsTaaBugs(cpuid, msr));
  }
}

TEST(BugTests, SsbPresence) {
  // Intel Atom x5-Z8350 (Airmont).
  // Expectation: not present.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelAtomX5_Z8350);
    arch::testing::FakeMsrIo msr;
    EXPECT_FALSE(arch::HasX86SsbBug(cpuid, msr));
  }

  // Intel Xeon E5-2690 V4 (Broadwell).
  {
    // Older microcode: No IA32_ARCH_CAPABILITIES.
    // Expectation: present.
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelXeonE5_2690_V4);
    arch::testing::FakeMsrIo msr;
    EXPECT_TRUE(arch::HasX86SsbBug(cpuid, msr));

    // Newer microcode: IA32_ARCH_CAPABILITIES, but still susceptible.
    MakeArchCapabilitiesAvailable(cpuid);
    msr.Populate(arch::X86Msr::IA32_ARCH_CAPABILITIES, 0);
    EXPECT_TRUE(arch::HasX86SsbBug(cpuid, msr));

    // Even newer microcode: IA32_ARCH_CAPABILITIES with SSB_NO.
    // Expectation: no longer present.
    msr.Populate(arch::X86Msr::IA32_ARCH_CAPABILITIES, uint64_t{1} << 4);
    EXPECT_FALSE(arch::HasX86SsbBug(cpuid, msr));
  }

  // AMD Ryzen 5 1500X.
  // Expectation: present.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen5_1500x);
    arch::testing::FakeMsrIo msr;
    EXPECT_TRUE(arch::HasX86SsbBug(cpuid, msr));
  }
}

TEST(BugTests, SsbMitigation) {
  // Intel Atom 330 (Bonnell).
  // Expectation: too old to mitigate.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelAtom330);
    arch::testing::FakeMsrIo msr;
    EXPECT_FALSE(arch::MitigateX86SsbBug(cpuid, msr));
    EXPECT_FALSE(arch::CanMitigateX86SsbBug(cpuid));
  }

  // Intel Xeon E5-2690 V4 (Broadwell).
  // Expectation: SSBD is advertised.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelXeonE5_2690_V4);
    arch::testing::FakeMsrIo msr;
    msr.Populate(arch::X86Msr::IA32_SPEC_CTRL, 0b11);

    EXPECT_TRUE(arch::MitigateX86SsbBug(cpuid, msr));
    EXPECT_EQ(0b111u, msr.Peek(arch::X86Msr::IA32_SPEC_CTRL));

    EXPECT_TRUE(arch::CanMitigateX86SsbBug(cpuid));
  }

  // AMD Ryzen 5 1500X.
  // Expectation: SSBD is not advertised (non-architectural means are used).
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen5_1500x);
    arch::testing::FakeMsrIo msr;
    msr.Populate(arch::X86Msr::MSRC001_1020, 0b10101);

    EXPECT_TRUE(arch::MitigateX86SsbBug(cpuid, msr));
    EXPECT_EQ(0b10101u | uint64_t{1} << 10, msr.Peek(arch::X86Msr::MSRC001_1020));

    EXPECT_TRUE(arch::CanMitigateX86SsbBug(cpuid));
  }

  // AMD Ryzen 9 3950X.
  // Expectation: SSBD is advertised.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen9_3950x);
    arch::testing::FakeMsrIo msr;
    msr.Populate(arch::X86Msr::IA32_SPEC_CTRL, 0b11);

    EXPECT_TRUE(arch::MitigateX86SsbBug(cpuid, msr));
    EXPECT_EQ(0b111u, msr.Peek(arch::X86Msr::IA32_SPEC_CTRL));

    EXPECT_TRUE(arch::CanMitigateX86SsbBug(cpuid));
  }

  // AMD Ryzen 9 3950X beneath WSL2.
  // Expectation: VIRT_SSBD is advertised.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen9_3950xWsl2);
    arch::testing::FakeMsrIo msr;
    msr.Populate(arch::X86Msr::MSR_VIRT_SPEC_CTRL, 0);

    EXPECT_TRUE(arch::MitigateX86SsbBug(cpuid, msr));
    EXPECT_EQ(0b100u, msr.Peek(arch::X86Msr::MSR_VIRT_SPEC_CTRL));

    EXPECT_TRUE(arch::CanMitigateX86SsbBug(cpuid));
  }
}

TEST(BugTests, SpectreV2Mitigation) {
  // Intel Core 2 6300.
  // Does not have IBRS or STIPB
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCore2_6300);
    arch::testing::FakeMsrIo msr;
    EXPECT_EQ(arch::SpectreV2Mitigation::kIbpbRetpoline,
              arch::GetPreferredSpectreV2Mitigation(cpuid, msr));
  }

  // Intel Xeon E5-2690 v4.
  // Has IBRS; does not have an always-on mode.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelXeonE5_2690_V4);
    arch::testing::FakeMsrIo msr;
    EXPECT_EQ(arch::SpectreV2Mitigation::kIbpbRetpoline,
              arch::GetPreferredSpectreV2Mitigation(cpuid, msr));

    // Suppose we perform a microcode update that enables the always-on
    // mode...
    MakeArchCapabilitiesAvailable(cpuid);
    msr.Populate(arch::X86Msr::IA32_ARCH_CAPABILITIES, 0b10);  // IBRS_ALL.
    EXPECT_EQ(arch::SpectreV2Mitigation::kIbrs, arch::GetPreferredSpectreV2Mitigation(cpuid, msr));
  }

  // AMD Ryzen 5 1500X.
  // Does not have IBRS or STIBP.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen5_1500x);
    arch::testing::FakeMsrIo msr;
    EXPECT_EQ(arch::SpectreV2Mitigation::kIbpbRetpoline,
              arch::GetPreferredSpectreV2Mitigation(cpuid, msr));
  }

  // AMD Ryzen 9 3950X.
  // Has STIBP; does not have an always-on mode.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen9_3950x);
    arch::testing::FakeMsrIo msr;
    EXPECT_EQ(arch::SpectreV2Mitigation::kIbpbRetpoline,
              arch::GetPreferredSpectreV2Mitigation(cpuid, msr));

    // Suppose we perform a microcode update that enables the always-on
    // mode...
    uint32_t features = cpuid.Read<arch::CpuidExtendedAmdFeatureFlagsB>().reg_value() |
                        uint32_t{1} << 17;  // STIBP_ALWAYS_ON
    cpuid.Populate(arch::CpuidExtendedAmdFeatureFlagsB::kLeaf, 0, arch::CpuidIo::kEbx, features);

    EXPECT_EQ(arch::SpectreV2Mitigation::kIbpbRetpolineStibp,
              arch::GetPreferredSpectreV2Mitigation(cpuid, msr));
  }
}

TEST(BugTests, MeltdownPresence) {
  // Intel Pentium N4200 (Goldmont).
  // Expectation: not present
  {
    // Older microcode: No IA32_ARCH_CAPABILITIES.
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelPentiumN4200);
    arch::testing::FakeMsrIo msr;
    EXPECT_FALSE(arch::HasX86MeltdownBug(cpuid, msr));

    // Newer microcode: IA32_ARCH_CAPABILITIES with RDCL_NO.
    MakeArchCapabilitiesAvailable(cpuid);
    msr.Populate(arch::X86Msr::IA32_ARCH_CAPABILITIES, uint64_t{1});
    EXPECT_FALSE(arch::HasX86MeltdownBug(cpuid, msr));
  }

  // AMD Ryzen 5 1500X.
  // Expectation: not present.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen5_1500x);
    arch::testing::FakeMsrIo msr;
    EXPECT_FALSE(arch::HasX86MeltdownBug(cpuid, msr));
  }

  // Intel Xeon E5-2690 v4.
  // Expectation: present
  {
    // Older microcode: No IA32_ARCH_CAPABILITIES.
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelXeonE5_2690_V4);
    arch::testing::FakeMsrIo msr;
    EXPECT_TRUE(arch::HasX86MeltdownBug(cpuid, msr));

    // Newer microcode: IA32_ARCH_CAPABILITIES, but still susceptible.
    MakeArchCapabilitiesAvailable(cpuid);
    msr.Populate(arch::X86Msr::IA32_ARCH_CAPABILITIES, 0);
    EXPECT_TRUE(arch::HasX86MeltdownBug(cpuid, msr));
  }
}

TEST(BugTests, L1tfPresence) {
  // Intel Atom x5-Z8350 (Airmont)
  // Expectation: not present
  {
    // Older microcode: No IA32_ARCH_CAPABILITIES.
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelAtomX5_Z8350);
    arch::testing::FakeMsrIo msr;
    EXPECT_FALSE(arch::HasX86L1tfBug(cpuid, msr));

    // Newer microcode: IA32_ARCH_CAPABILITIES with RDCL_NO.
    msr.Populate(arch::X86Msr::IA32_ARCH_CAPABILITIES, uint64_t{1});
    EXPECT_FALSE(arch::HasX86L1tfBug(cpuid, msr));
  }

  // AMD Ryzen 5 1500X.
  // Expectation: not present.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen5_1500x);
    arch::testing::FakeMsrIo msr;
    EXPECT_FALSE(arch::HasX86L1tfBug(cpuid, msr));
  }

  // Intel Xeon E5-2690 v4.
  // Expectation: present
  {
    // Older microcode: No IA32_ARCH_CAPABILITIES.
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelXeonE5_2690_V4);
    arch::testing::FakeMsrIo msr;
    EXPECT_TRUE(arch::HasX86L1tfBug(cpuid, msr));

    // Newer microcode: IA32_ARCH_CAPABILITIES, but still susceptible.
    MakeArchCapabilitiesAvailable(cpuid);
    msr.Populate(arch::X86Msr::IA32_ARCH_CAPABILITIES, 0);
    EXPECT_TRUE(arch::HasX86L1tfBug(cpuid, msr));
  }
}

}  // namespace
