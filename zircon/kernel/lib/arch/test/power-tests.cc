// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/testing/x86/fake-cpuid.h>
#include <lib/arch/testing/x86/fake-msr.h>
#include <lib/arch/x86/power.h>

#include <gtest/gtest.h>

namespace {

using arch::testing::X86Microprocessor;

TEST(X86PowerTests, SetX86CpuTurboState) {
  // Describe...
  constexpr uint64_t kIntelTurboMaybeSupportedAndOn = 0b10101010101;
  constexpr uint64_t kIntelTurboSupportedAndOff =
      kIntelTurboMaybeSupportedAndOn | (uint64_t{1} << 38);
  constexpr uint64_t kAmdTurboOn = 0b11011011011;
  constexpr uint64_t kAmdTurboOff = kAmdTurboOn | (uint64_t{1} << 25);

  // Intel Core i3-6100; supported.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCoreI3_6100);
    arch::testing::FakeMsrIo msr;
    msr.Populate(arch::X86Msr::IA32_MISC_ENABLE, kIntelTurboSupportedAndOff);

    EXPECT_TRUE(arch::SetX86CpuTurboState(cpuid, msr, true));
    EXPECT_EQ(kIntelTurboMaybeSupportedAndOn, msr.Peek(arch::X86Msr::IA32_MISC_ENABLE));

    EXPECT_TRUE(arch::SetX86CpuTurboState(cpuid, msr, false));
    EXPECT_EQ(kIntelTurboSupportedAndOff, msr.Peek(arch::X86Msr::IA32_MISC_ENABLE));
  }

  // Intel Core i3-3240; unsupported.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCoreI3_3240);
    arch::testing::FakeMsrIo msr;
    // In this case, the maybe-supported-and-on state is the unsupported state.
    constexpr uint64_t kUnsupportedState = kIntelTurboMaybeSupportedAndOn;
    msr.Populate(arch::X86Msr::IA32_MISC_ENABLE, kUnsupportedState);

    EXPECT_FALSE(arch::SetX86CpuTurboState(cpuid, msr, true));
    EXPECT_EQ(kUnsupportedState, msr.Peek(arch::X86Msr::IA32_MISC_ENABLE));

    EXPECT_FALSE(arch::SetX86CpuTurboState(cpuid, msr, false));
    EXPECT_EQ(kUnsupportedState, msr.Peek(arch::X86Msr::IA32_MISC_ENABLE));
  }

  // AMD Ryzen Threadripper 2970WX; supported.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzenThreadripper2970wx);
    arch::testing::FakeMsrIo msr;
    msr.Populate(arch::X86Msr::MSRC001_0015, kAmdTurboOff)
        .Populate(arch::X86Msr::IA32_MISC_ENABLE, 0);

    EXPECT_TRUE(arch::SetX86CpuTurboState(cpuid, msr, true));
    EXPECT_EQ(kAmdTurboOn, msr.Peek(arch::X86Msr::MSRC001_0015));

    EXPECT_TRUE(arch::SetX86CpuTurboState(cpuid, msr, false));
    EXPECT_EQ(kAmdTurboOff, msr.Peek(arch::X86Msr::MSRC001_0015));
  }

  // AMD Ryzen Threadripper 1950X; unsupported.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzenThreadripper1950x);
    arch::testing::FakeMsrIo msr;
    msr.Populate(arch::X86Msr::MSRC001_0015, kAmdTurboOff)
        .Populate(arch::X86Msr::IA32_MISC_ENABLE, 0);

    EXPECT_FALSE(arch::SetX86CpuTurboState(cpuid, msr, true));
    EXPECT_EQ(kAmdTurboOff, msr.Peek(arch::X86Msr::MSRC001_0015));

    EXPECT_FALSE(arch::SetX86CpuTurboState(cpuid, msr, false));
    EXPECT_EQ(kAmdTurboOff, msr.Peek(arch::X86Msr::MSRC001_0015));
  }
}

}  // namespace
