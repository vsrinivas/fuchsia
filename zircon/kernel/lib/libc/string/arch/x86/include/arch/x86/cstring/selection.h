// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_LIBC_STRING_ARCH_X86_INCLUDE_ARCH_X86_CSTRING_SELECTION_H_
#define ZIRCON_KERNEL_LIB_LIBC_STRING_ARCH_X86_INCLUDE_ARCH_X86_CSTRING_SELECTION_H_

#include <lib/arch/x86/cpuid.h>

#include <string_view>

template <typename CpuidIoProvider>
inline bool StoreByByte(CpuidIoProvider&& cpuid) {
  const auto ebx = cpuid.template Read<arch::CpuidExtendedFeatureFlagsB>();
  const auto edx = cpuid.template Read<arch::CpuidExtendedFeatureFlagsD>();
  // Whether the "Enhanced" or "Fast Short" optimizations are present.
  return ebx.erms() || edx.fsrm();
}

// Returns the appropriate code patching alternative of `memcpy()`.
template <typename CpuidIoProvider>
inline std::string_view SelectX86MemcpyAlternative(CpuidIoProvider&& cpuid) {
  if (StoreByByte(cpuid)) {
    return "memcpy_movsb";
  }
  return "memcpy_movsq";
}

// Returns the appropriate code patching alternative of `memset()`.
template <typename CpuidIoProvider>
inline std::string_view SelectX86MemsetAlternative(CpuidIoProvider&& cpuid) {
  if (StoreByByte(cpuid)) {
    return "memset_stosb";
  }
  return "memset_stosq";
}

#endif  // ZIRCON_KERNEL_LIB_LIBC_STRING_ARCH_X86_INCLUDE_ARCH_X86_CSTRING_SELECTION_H_
