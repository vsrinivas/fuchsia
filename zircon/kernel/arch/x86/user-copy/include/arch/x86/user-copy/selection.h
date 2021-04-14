// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_USER_COPY_INCLUDE_ARCH_X86_USER_COPY_SELECTION_H_
#define ZIRCON_KERNEL_ARCH_X86_USER_COPY_INCLUDE_ARCH_X86_USER_COPY_SELECTION_H_

#include <lib/arch/x86/cpuid.h>

#include <string_view>

// Returns the appropriate code patching alternative of
// `_x86_copy_to_or_from_user()`.
template <typename CpuidIoProvider>
inline std::string_view SelectX86UserCopyAlternative(CpuidIoProvider&& cpuid) {
  const auto ebx = cpuid.template Read<arch::CpuidExtendedFeatureFlagsB>();
  const auto edx = cpuid.template Read<arch::CpuidExtendedFeatureFlagsD>();
  const bool is_zen = arch::GetMicroarchitecture(cpuid) == arch::Microarchitecture::kAmdFamilyZen;

  // Whether the "Enhanced" or "Fast Short" `rep mov` optimizations are present
  // - or whether this is an AMD Zen (for which previous measurements indicated
  // that moving byte by byte was on the whole faster.)
  if (ebx.erms() || edx.fsrm() || is_zen) {
    if (ebx.smap()) {
      return "_x86_copy_to_or_from_user_movsb_smap";
    }
    return "_x86_copy_to_or_from_user_movsb";
  } else {
    if (ebx.smap()) {
      return "_x86_copy_to_or_from_user_movsq_smap";
    }
    return "_x86_copy_to_or_from_user_movsq";
  }
}

#endif  // ZIRCON_KERNEL_ARCH_X86_USER_COPY_INCLUDE_ARCH_X86_USER_COPY_SELECTION_H_
