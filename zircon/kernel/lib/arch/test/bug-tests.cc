// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/testing/x86/fake-cpuid.h>
#include <lib/arch/x86/bug.h>

#include <gtest/gtest.h>

namespace {

using arch::testing::X86Microprocessor;

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

}  // namespace
