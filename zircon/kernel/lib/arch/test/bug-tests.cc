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

  // Intel Atom x5-Z8350 (Silvermont)
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

}  // namespace
