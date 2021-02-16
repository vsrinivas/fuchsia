// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/testing/x86/fake-cpuid.h>
#include <lib/arch/testing/x86/fake-msr.h>
#include <lib/arch/x86/lbr.h>

#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

namespace {

using arch::testing::X86Microprocessor;

// Values of IA32_DEBUGCTL MSR representing an unenabled state (with random
// bits set), an enabled state - differing from the former by bits 1 (enable
// LBRs) and 11 (freeze LBR recording on PMIs) - and a disabled state,
// differing from the enabled state only by bit 1.
constexpr uint64_t kUnenabledDebugctl = 0b01111000010;
constexpr uint64_t kEnabledDebugctl = kUnenabledDebugctl | 0b100000000001;
constexpr uint64_t kDisabledDebugctl = kEnabledDebugctl ^ 0b000000000001;

// Values of MSR_LBR_SELECT, varied by whether recording for userspace or
// kernel is configured, and whether callstack profiling is enabled.
constexpr uint64_t kLbrSelectUserspaceBasic = 0b0011000101;
constexpr uint64_t kLbrSelectKernelBasic = 0b0011000110;
constexpr uint64_t kLbrSelectUserspaceProfiling = 0b1011000101;
constexpr uint64_t kLbrSelectKernelProfiling = 0b1011000110;

struct Lbr {
  uint64_t from;
  uint64_t to;
  uint64_t info;
  uint32_t idx;
};

void PopulateLbrs(arch::testing::FakeMsrIo& msr, uint32_t stack_size,
                  const std::vector<Lbr>& lbrs) {
  for (uint32_t i = 0; i < stack_size; ++i) {
    // MSR_LASTBRANCH_N_FROM_IP
    msr.Populate(static_cast<arch::X86Msr>(uint32_t{0x0000'0680} + i), 0)
        // MSR_LASTBRANCH_N_TO_IP.
        .Populate(static_cast<arch::X86Msr>(uint32_t{0x0000'06c0} + i), 0)
        // MSR_LBR_INFO_N.
        .Populate(static_cast<arch::X86Msr>(uint32_t{0x0000'0dc0} + i), 0);
  }
  for (const auto& lbr : lbrs) {
    ASSERT_LT(lbr.idx, stack_size);
    msr.Populate(static_cast<arch::X86Msr>(uint32_t{0x0000'0680} + lbr.idx), lbr.from)
        .Populate(static_cast<arch::X86Msr>(uint32_t{0x0000'06c0} + lbr.idx), lbr.to)
        .Populate(static_cast<arch::X86Msr>(uint32_t{0x0000'0dc0} + lbr.idx), lbr.info);
  }
}

TEST(LbrTests, ToAndFromFields) {
  const auto to = arch::LbrToIpMsr::Get(0).FromValue(0xffff'ffff'ffff'ffff);
  const auto from = arch::LbrFromIpMsr::Get(0).FromValue(0xffff'ffff'ffff'ffff);

  // X86LbrFormat::k32Bit.
  // TO: just the IP.
  // FROM: just the IP
  {
    arch::X86LbrFormat fmt = arch::X86LbrFormat::k32Bit;
    EXPECT_EQ(0xffff'ffff'ffff'ffffu, to.ip(fmt));  // 64 bits.
    EXPECT_FALSE(to.cycle_count(fmt).has_value());

    EXPECT_EQ(0xffff'ffff'ffff'ffffu, from.ip(fmt));  // 64 bits.
    EXPECT_FALSE(from.tsx_abort(fmt).has_value());
    EXPECT_FALSE(from.in_tsx(fmt).has_value());
    EXPECT_FALSE(from.mispredicted(fmt).has_value());
  }

  // X86LbrFormat::k64BitLip.
  // TO: just the IP.
  // FROM: just the IP
  {
    arch::X86LbrFormat fmt = arch::X86LbrFormat::k64BitLip;
    EXPECT_EQ(0xffff'ffff'ffff'ffffu, to.ip(fmt));  // 64 bits.
    EXPECT_FALSE(to.cycle_count(fmt).has_value());

    EXPECT_EQ(0xffff'ffff'ffff'ffffu, from.ip(fmt));  // 64 bits.
    EXPECT_FALSE(from.tsx_abort(fmt).has_value());
    EXPECT_FALSE(from.in_tsx(fmt).has_value());
    EXPECT_FALSE(from.mispredicted(fmt).has_value());
  }

  // X86LbrFormat::k64BitEip.
  // TO: just the IP.
  // FROM: just the IP
  {
    arch::X86LbrFormat fmt = arch::X86LbrFormat::k64BitEip;
    EXPECT_EQ(0xffff'ffff'ffff'ffffu, to.ip(fmt));  // 64 bits.
    EXPECT_FALSE(to.cycle_count(fmt).has_value());

    EXPECT_EQ(0xffff'ffff'ffff'ffffu, from.ip(fmt));  // 64 bits.
    EXPECT_FALSE(from.tsx_abort(fmt).has_value());
    EXPECT_FALSE(from.in_tsx(fmt).has_value());
    EXPECT_FALSE(from.mispredicted(fmt).has_value());
  }

  // X86LbrFormat::k64BitEipWithFlags
  // TO: just the IP.
  // FROM: IP and misprediction bit.
  {
    arch::X86LbrFormat fmt = arch::X86LbrFormat::k64BitEipWithFlags;
    EXPECT_EQ(0xffff'ffff'ffff'ffffu, to.ip(fmt));  // 64 bits.
    EXPECT_FALSE(to.cycle_count(fmt).has_value());

    EXPECT_EQ(0x7fff'ffff'ffff'ffffu, from.ip(fmt));  // 63 bits.
    EXPECT_FALSE(from.tsx_abort(fmt).has_value());
    EXPECT_FALSE(from.in_tsx(fmt).has_value());
    ASSERT_TRUE(from.mispredicted(fmt).has_value());
    EXPECT_EQ(1, *from.mispredicted(fmt));  // 1 bit.
  }

  // X86LbrFormat::k64BitEipWithFlagsTsx
  // TO: just the IP.
  // FROM: IP, misprediction bit, and TSX info.
  {
    arch::X86LbrFormat fmt = arch::X86LbrFormat::k64BitEipWithFlagsTsx;
    EXPECT_EQ(0xffff'ffff'ffff'ffffu, to.ip(fmt));  // 64 bits.
    EXPECT_FALSE(to.cycle_count(fmt).has_value());

    EXPECT_EQ(0x1fff'ffff'ffff'ffffu, from.ip(fmt));  // 61 bits.
    ASSERT_TRUE(from.tsx_abort(fmt).has_value());
    EXPECT_EQ(1, *from.tsx_abort(fmt));  // 1 bit.
    ASSERT_TRUE(from.in_tsx(fmt).has_value());
    EXPECT_EQ(1, *from.in_tsx(fmt));  // 1 bit.
    ASSERT_TRUE(from.mispredicted(fmt).has_value());
    EXPECT_EQ(1, *from.mispredicted(fmt));  // 1 bit.
  }

  // X86LbrFormat::k64BitEipWithInfo
  // TO: just the IP.
  // FROM: just the IP
  {
    arch::X86LbrFormat fmt = arch::X86LbrFormat::k64BitEipWithInfo;
    EXPECT_EQ(0xffff'ffff'ffff'ffffu, to.ip(fmt));  // 64 bits.
    EXPECT_FALSE(to.cycle_count(fmt).has_value());

    EXPECT_EQ(0xffff'ffff'ffff'ffffu, from.ip(fmt));  // 64 bits.
    EXPECT_FALSE(from.tsx_abort(fmt).has_value());
    EXPECT_FALSE(from.in_tsx(fmt).has_value());
    EXPECT_FALSE(from.mispredicted(fmt).has_value());
  }

  // X86LbrFormat::k64BitLipWithFlagsCycles.
  // TO: IP and cycle count.
  // FROM: IP and misprediction bit.
  {
    arch::X86LbrFormat fmt = arch::X86LbrFormat::k64BitLipWithFlagsCycles;
    EXPECT_EQ(0xffff'ffff'ffffu, to.ip(fmt));  // 48 bits.
    ASSERT_TRUE(to.cycle_count(fmt).has_value());
    EXPECT_EQ(0xffffu, *to.cycle_count(fmt));

    EXPECT_EQ(0x7fff'ffff'ffff'ffffu, from.ip(fmt));  // 63 bits.
    EXPECT_FALSE(from.tsx_abort(fmt).has_value());
    EXPECT_FALSE(from.in_tsx(fmt).has_value());
    ASSERT_TRUE(from.mispredicted(fmt).has_value());
    EXPECT_EQ(1, *from.mispredicted(fmt));  // 1 bit.
  }

  // X86LbrFormat::k64BitLipWithInfo.
  // TO: just the IP.
  // FROM: just the IP
  {
    arch::X86LbrFormat fmt = arch::X86LbrFormat::k64BitLipWithInfo;
    EXPECT_EQ(0xffff'ffff'ffff'ffffu, to.ip(fmt));  // 64 bits.
    EXPECT_FALSE(to.cycle_count(fmt).has_value());

    EXPECT_EQ(0xffff'ffff'ffff'ffffu, from.ip(fmt));  // 64 bits.
    EXPECT_FALSE(from.tsx_abort(fmt).has_value());
    EXPECT_FALSE(from.in_tsx(fmt).has_value());
    EXPECT_FALSE(from.mispredicted(fmt).has_value());
  }
}

TEST(LbrTests, Unsupported) {
  // AMD does not support LBRs.
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen9_3950x);
  arch::testing::FakeMsrIo msr;

  arch::LbrStack stack(cpuid);
  EXPECT_FALSE(stack.is_supported());
  EXPECT_FALSE(stack.is_enabled(msr));
  EXPECT_EQ(0u, stack.size());
}

TEST(LbrTests, Supported) {
  // Intel Core 2; stack size of 4.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCore2_6300);

    arch::LbrStack stack(cpuid);
    EXPECT_TRUE(stack.is_supported());
    EXPECT_EQ(4u, stack.size());
  }

  // Intel Airmont; stack size of 8.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelAtomX5_Z8350);

    arch::LbrStack stack(cpuid);
    EXPECT_TRUE(stack.is_supported());
    EXPECT_EQ(8u, stack.size());
  }

  // Intel Nehalem; stack size of 16.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelXeonE5520);

    arch::LbrStack stack(cpuid);
    EXPECT_TRUE(stack.is_supported());
    EXPECT_EQ(16u, stack.size());
  }

  // Intel Skylake; stack size of 32.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCoreI3_6100);

    arch::LbrStack stack(cpuid);
    EXPECT_TRUE(stack.is_supported());
    EXPECT_EQ(32u, stack.size());
  }
}

TEST(LbrTests, Enabling) {
  // Intel Core 2; no callstack profiling.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCore2_6300);
    arch::testing::FakeMsrIo msr;
    msr.Populate(arch::X86Msr::IA32_DEBUGCTL, kUnenabledDebugctl)
        .Populate(arch::X86Msr::MSR_LBR_SELECT, 0);

    arch::LbrStack stack(cpuid);
    EXPECT_FALSE(stack.is_enabled(msr));

    stack.Enable(msr, /*for_user=*/true);
    EXPECT_TRUE(stack.is_enabled(msr));
    EXPECT_EQ(kEnabledDebugctl, msr.Peek(arch::X86Msr::IA32_DEBUGCTL));
    EXPECT_EQ(kLbrSelectUserspaceBasic, msr.Peek(arch::X86Msr::MSR_LBR_SELECT));

    stack.Enable(msr, /*for_user=*/false);
    EXPECT_TRUE(stack.is_enabled(msr));
    EXPECT_EQ(kEnabledDebugctl, msr.Peek(arch::X86Msr::IA32_DEBUGCTL));
    EXPECT_EQ(kLbrSelectKernelBasic, msr.Peek(arch::X86Msr::MSR_LBR_SELECT));

    stack.Disable(msr);
    EXPECT_FALSE(stack.is_enabled(msr));
    EXPECT_EQ(kDisabledDebugctl, msr.Peek(arch::X86Msr::IA32_DEBUGCTL));
  }

  // Intel Airmont; no callstack profiling.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelAtomX5_Z8350);
    arch::testing::FakeMsrIo msr;
    msr.Populate(arch::X86Msr::IA32_DEBUGCTL, kUnenabledDebugctl)
        .Populate(arch::X86Msr::MSR_LBR_SELECT, 0);

    arch::LbrStack stack(cpuid);
    EXPECT_FALSE(stack.is_enabled(msr));

    stack.Enable(msr, /*for_user=*/true);
    EXPECT_TRUE(stack.is_enabled(msr));
    EXPECT_EQ(kEnabledDebugctl, msr.Peek(arch::X86Msr::IA32_DEBUGCTL));
    EXPECT_EQ(kLbrSelectUserspaceBasic, msr.Peek(arch::X86Msr::MSR_LBR_SELECT));

    stack.Enable(msr, /*for_user=*/false);
    EXPECT_TRUE(stack.is_enabled(msr));
    EXPECT_EQ(kEnabledDebugctl, msr.Peek(arch::X86Msr::IA32_DEBUGCTL));
    EXPECT_EQ(kLbrSelectKernelBasic, msr.Peek(arch::X86Msr::MSR_LBR_SELECT));

    stack.Disable(msr);
    EXPECT_FALSE(stack.is_enabled(msr));
    EXPECT_EQ(kDisabledDebugctl, msr.Peek(arch::X86Msr::IA32_DEBUGCTL));
  }

  // Intel Nehalem; no callstack profiling.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelXeonE5520);
    arch::testing::FakeMsrIo msr;
    msr.Populate(arch::X86Msr::IA32_DEBUGCTL, kUnenabledDebugctl)
        .Populate(arch::X86Msr::MSR_LBR_SELECT, 0);

    arch::LbrStack stack(cpuid);
    EXPECT_FALSE(stack.is_enabled(msr));

    stack.Enable(msr, /*for_user=*/true);
    EXPECT_TRUE(stack.is_enabled(msr));
    EXPECT_EQ(kEnabledDebugctl, msr.Peek(arch::X86Msr::IA32_DEBUGCTL));
    EXPECT_EQ(kLbrSelectUserspaceBasic, msr.Peek(arch::X86Msr::MSR_LBR_SELECT));

    stack.Enable(msr, /*for_user=*/false);
    EXPECT_TRUE(stack.is_enabled(msr));
    EXPECT_EQ(kEnabledDebugctl, msr.Peek(arch::X86Msr::IA32_DEBUGCTL));
    EXPECT_EQ(kLbrSelectKernelBasic, msr.Peek(arch::X86Msr::MSR_LBR_SELECT));

    stack.Disable(msr);
    EXPECT_FALSE(stack.is_enabled(msr));
    EXPECT_EQ(kDisabledDebugctl, msr.Peek(arch::X86Msr::IA32_DEBUGCTL));
  }

  // Intel Skylake; callstack profiling.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCoreI3_6100);
    arch::testing::FakeMsrIo msr;
    msr.Populate(arch::X86Msr::IA32_DEBUGCTL, kUnenabledDebugctl)
        .Populate(arch::X86Msr::MSR_LBR_SELECT, 0);

    arch::LbrStack stack(cpuid);
    EXPECT_FALSE(stack.is_enabled(msr));

    stack.Enable(msr, /*for_user=*/true);
    EXPECT_TRUE(stack.is_enabled(msr));
    EXPECT_EQ(kEnabledDebugctl, msr.Peek(arch::X86Msr::IA32_DEBUGCTL));
    EXPECT_EQ(kLbrSelectUserspaceProfiling, msr.Peek(arch::X86Msr::MSR_LBR_SELECT));

    stack.Enable(msr, /*for_user=*/false);
    EXPECT_TRUE(stack.is_enabled(msr));
    EXPECT_EQ(kEnabledDebugctl, msr.Peek(arch::X86Msr::IA32_DEBUGCTL));
    EXPECT_EQ(kLbrSelectKernelProfiling, msr.Peek(arch::X86Msr::MSR_LBR_SELECT));

    stack.Disable(msr);
    EXPECT_FALSE(stack.is_enabled(msr));
    EXPECT_EQ(kDisabledDebugctl, msr.Peek(arch::X86Msr::IA32_DEBUGCTL));
  }
}

TEST(LbrTests, Iteration) {
  // Intel Nehalem; stack size of 16; k64BitEipWithFlags.
  {
    constexpr uint32_t kStackSize = 16;
    constexpr uint32_t kTopOfStack = 11;  // Arbitrary.

    // An empty LBR in the k64BitEipWithFlags format.
    constexpr arch::LastBranchRecord kEmptyLbr = {
        .from = 0,
        .to = 0,
        .mispredicted = false,
    };

    constexpr uint64_t kLbrFrom1 = 0x8000'aaaa'bbbb'cccc;  // Mispredicted.
    constexpr uint64_t kLbrTo1 = 0x1234'0000'4567'0000;
    constexpr uint32_t kLbrIdx1 = 2;
    constexpr arch::LastBranchRecord kExpectedLbr1 = {
        .from = 0x0000'aaaa'bbbb'cccc,
        .to = kLbrTo1,
        .mispredicted = true,
    };

    constexpr uint64_t kLbrFrom2 = 0x0000'cccc'aaaa'bbbb;
    constexpr uint64_t kLbrTo2 = 0x0000'1234'0000'4567;
    constexpr uint32_t kLbrIdx2 = 5;
    constexpr arch::LastBranchRecord kExpectedLbr2 = {
        .from = kLbrFrom2,
        .to = kLbrTo2,
        .mispredicted = false,
    };

    constexpr uint64_t kLbrFrom3 = 0x8000'bbbb'cccc'aaaa;  // Mispredicted.
    constexpr uint64_t kLbrTo3 = 0x4567'0000'1234'0000;
    constexpr uint32_t kLbrIdx3 = 12;
    constexpr arch::LastBranchRecord kExpectedLbr3 = {
        .from = 0x0000'bbbb'cccc'aaaa,
        .to = kLbrTo3,
        .mispredicted = true,
    };

    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelXeonE5520);
    arch::testing::FakeMsrIo msr;
    msr.Populate(arch::X86Msr::IA32_DEBUGCTL, kEnabledDebugctl)  // Already enabled.
        .Populate(arch::X86Msr::MSR_LASTBRANCH_TOS, kTopOfStack)
        .Populate(arch::X86Msr::IA32_PERF_CAPABILITIES, 0b000011);  // k64BitEipWithFlags.

    arch::LbrStack stack(cpuid);
    ASSERT_NO_FATAL_FAILURE(PopulateLbrs(msr, kStackSize,
                                         {
                                             {
                                                 .from = kLbrFrom1,
                                                 .to = kLbrTo1,
                                                 .idx = kLbrIdx1,
                                             },
                                             {
                                                 .from = kLbrFrom2,
                                                 .to = kLbrTo2,
                                                 .idx = kLbrIdx2,
                                             },
                                             {
                                                 .from = kLbrFrom3,
                                                 .to = kLbrTo3,
                                                 .idx = kLbrIdx3,
                                             },
                                         }));

    std::vector<arch::LastBranchRecord> lbrs;
    stack.ForEachRecord(msr, [&lbrs](const arch::LastBranchRecord& lbr) { lbrs.push_back(lbr); });
    ASSERT_EQ(kStackSize, lbrs.size());
    // In terms of original indices, we expect the LBRs to be ordered as
    // [kTopOfStack,..., kStackSize - 1, 0, ..., kTopOfStack).
    // A leftward shift of `kStackSize - kTopOfStack` should normalize the
    // recording.
    std::rotate(lbrs.begin(), lbrs.begin() + kStackSize - kTopOfStack, lbrs.end());

    for (uint32_t i = 0; i < kStackSize; ++i) {
      const arch::LastBranchRecord& actual = lbrs[i];
      arch::LastBranchRecord expected;
      switch (i) {
        case kLbrIdx1:
          expected = kExpectedLbr1;
          break;
        case kLbrIdx2:
          expected = kExpectedLbr2;
          break;
        case kLbrIdx3:
          expected = kExpectedLbr3;
          break;
        default:
          expected = kEmptyLbr;
          break;
      }

      EXPECT_EQ(expected.from, actual.from) << i;
      EXPECT_EQ(expected.to, actual.to) << i;
      EXPECT_EQ(expected.mispredicted, actual.mispredicted) << i;
      EXPECT_EQ(expected.cycle_count, actual.cycle_count) << i;
      EXPECT_EQ(expected.in_tsx, actual.in_tsx) << i;
      EXPECT_EQ(expected.tsx_abort, actual.tsx_abort) << i;
    }
  }

  // Intel Skylake; stack size of 32; k64BitEipWithInfo.
  {
    constexpr uint32_t kStackSize = 32;
    constexpr uint32_t kTopOfStack = 25;  // Arbitrary.

    // An empty LBR in the k64BitEipWithInfo format.
    constexpr arch::LastBranchRecord kEmptyLbr = {
        .from = 0,
        .to = 0,
        .mispredicted = false,
        .cycle_count = 0,
        .in_tsx = false,
        .tsx_abort = false,
    };

    constexpr uint64_t kLbrFrom1 = 0x0000'aaaa'bbbb'cccc;
    constexpr uint64_t kLbrTo1 = 0x1234'0000'4567'0000;
    constexpr uint64_t kLbrInfo1 = 0xc000'0000'0000'0007;
    constexpr uint32_t kLbrIdx1 = 12;
    constexpr arch::LastBranchRecord kExpectedLbr1 = {
        .from = kLbrFrom1,
        .to = kLbrTo1,
        .mispredicted = true,
        .cycle_count = 7,
        .in_tsx = true,
        .tsx_abort = false,
    };

    constexpr uint64_t kLbrFrom2 = 0x0000'cccc'aaaa'bbbb;
    constexpr uint64_t kLbrTo2 = 0x0000'1234'0000'4567;
    constexpr uint64_t kLbrInfo2 = 0x6000'0000'0000'0019;
    constexpr uint32_t kLbrIdx2 = 14;
    constexpr arch::LastBranchRecord kExpectedLbr2 = {
        .from = kLbrFrom2,
        .to = kLbrTo2,
        .mispredicted = false,
        .cycle_count = 25,
        .in_tsx = true,
        .tsx_abort = true,
    };

    constexpr uint64_t kLbrFrom3 = 0x0000'bbbb'cccc'aaaa;
    constexpr uint64_t kLbrTo3 = 0x4567'0000'1234'0000;
    constexpr uint64_t kLbrInfo3 = 0x8000'0000'0000'000f;
    constexpr uint32_t kLbrIdx3 = 27;
    constexpr arch::LastBranchRecord kExpectedLbr3 = {
        .from = kLbrFrom3,
        .to = kLbrTo3,
        .mispredicted = true,
        .cycle_count = 15,
        .in_tsx = false,
        .tsx_abort = false,
    };

    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCoreI3_6100);
    arch::testing::FakeMsrIo msr;
    msr.Populate(arch::X86Msr::IA32_DEBUGCTL, kEnabledDebugctl)  // Already enabled.
        .Populate(arch::X86Msr::MSR_LASTBRANCH_TOS, kTopOfStack)
        .Populate(arch::X86Msr::IA32_PERF_CAPABILITIES, 0b000101);  // k64BitEipWithInfo.

    arch::LbrStack stack(cpuid);
    ASSERT_NO_FATAL_FAILURE(PopulateLbrs(msr, kStackSize,
                                         {
                                             {
                                                 .from = kLbrFrom1,
                                                 .to = kLbrTo1,
                                                 .info = kLbrInfo1,
                                                 .idx = kLbrIdx1,
                                             },
                                             {
                                                 .from = kLbrFrom2,
                                                 .to = kLbrTo2,
                                                 .info = kLbrInfo2,
                                                 .idx = kLbrIdx2,
                                             },
                                             {
                                                 .from = kLbrFrom3,
                                                 .to = kLbrTo3,
                                                 .info = kLbrInfo3,
                                                 .idx = kLbrIdx3,
                                             },
                                         }));

    std::vector<arch::LastBranchRecord> lbrs;
    stack.ForEachRecord(msr, [&lbrs](const arch::LastBranchRecord& lbr) { lbrs.push_back(lbr); });
    ASSERT_EQ(kStackSize, lbrs.size());
    // In terms of original indices, we expect the LBRs to be ordered as
    // [kTopOfStack,..., kStackSize - 1, 0, ..., kTopOfStack).
    // A leftward shift of `kStackSize - kTopOfStack` should normalize the
    // recording.
    std::rotate(lbrs.begin(), lbrs.begin() + kStackSize - kTopOfStack, lbrs.end());

    for (uint32_t i = 0; i < kStackSize; ++i) {
      const arch::LastBranchRecord& actual = lbrs[i];
      arch::LastBranchRecord expected;
      switch (i) {
        case kLbrIdx1:
          expected = kExpectedLbr1;
          break;
        case kLbrIdx2:
          expected = kExpectedLbr2;
          break;
        case kLbrIdx3:
          expected = kExpectedLbr3;
          break;
        default:
          expected = kEmptyLbr;
          break;
      }

      EXPECT_EQ(expected.from, actual.from) << i;
      EXPECT_EQ(expected.to, actual.to) << i;
      EXPECT_EQ(expected.mispredicted, actual.mispredicted) << i;
      EXPECT_EQ(expected.cycle_count, actual.cycle_count) << i;
      EXPECT_EQ(expected.in_tsx, actual.in_tsx) << i;
      EXPECT_EQ(expected.tsx_abort, actual.tsx_abort) << i;
    }
  }
}

}  // namespace
