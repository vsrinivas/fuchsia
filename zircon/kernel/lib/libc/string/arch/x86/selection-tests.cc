// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/testing/x86/fake-cpuid.h>

#include <arch/x86/cstring/selection.h>
#include <gtest/gtest.h>

namespace {

using namespace std::literals;

using arch::testing::X86Microprocessor;

TEST(X86CstringTests, Selection) {
  // Intel Core2 6300: no ERMS.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCore2_6300);
    EXPECT_EQ("memcpy_movsq"sv, SelectX86MemcpyAlternative(cpuid));
    EXPECT_EQ("memset_stosq"sv, SelectX86MemsetAlternative(cpuid));
  }

  // Intel Core i3-6100: ERMS.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCoreI3_6100);
    EXPECT_EQ("memcpy_movsb"sv, SelectX86MemcpyAlternative(cpuid));
    EXPECT_EQ("memset_stosb"sv, SelectX86MemsetAlternative(cpuid));
  }

  // AMD Ryzen 5 1500X: No ERMS.
  {
    arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen5_1500x);
    EXPECT_EQ("memcpy_movsq"sv, SelectX86MemcpyAlternative(cpuid));
    EXPECT_EQ("memset_stosq"sv, SelectX86MemsetAlternative(cpuid));
  }
}

}  // namespace
