// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/testing/x86/fake-cpuid.h>

#include <arch/x86/user-copy/selection.h>
#include <gtest/gtest.h>

namespace {

using namespace std::literals;

using arch::testing::X86Microprocessor;

TEST(X86UserCopyTests, Selection) {
  // Intel Core2 6300: no ERMS, no SMAP.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCore2_6300);
    EXPECT_EQ("_x86_copy_to_or_from_user_movsq"sv, SelectX86UserCopyAlternative(cpuid));
  }

  // Intel Core i3-3240: ERMS, no SMAP.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCoreI3_3240);
    EXPECT_EQ("_x86_copy_to_or_from_user_movsb"sv, SelectX86UserCopyAlternative(cpuid));
  }

  // Intel Core i3-6100: ERMS, SMAP.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCoreI3_6100);
    EXPECT_EQ("_x86_copy_to_or_from_user_movsb_smap"sv, SelectX86UserCopyAlternative(cpuid));
  }

  // AMD A10-7870K: Pre-Zen, no SMAP.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdA10_7870k);
    EXPECT_EQ("_x86_copy_to_or_from_user_movsq"sv, SelectX86UserCopyAlternative(cpuid));
  }

  // AMD Ryzen 5 1500X: Zen, SMAP.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen5_1500x);
    EXPECT_EQ("_x86_copy_to_or_from_user_movsb_smap"sv, SelectX86UserCopyAlternative(cpuid));
  }
}

}  // namespace
