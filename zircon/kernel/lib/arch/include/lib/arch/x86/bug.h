// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_BUG_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_BUG_H_

#include <lib/arch/x86/cpuid.h>

// This file contains utilities related to probing and mitigating architectural
// bugs and vulnerabilities.
//
// In general, we cannot rely on the official means of enumerating whether a
// vulnerability is present. For example, it might only be enumerable after
// certain microcode updates are performed. Accordingly, if we cannot get an
// definitive "is not vulnerable" from the official means, we fall back to
// pessimistically assigning vulnerability on the basis of microarchitecture,
// making implicit reference to the following documents:
//
// * Intel:
//   https://software.intel.com/security-software-guidance/processors-affected-transient-execution-attack-mitigation-product-cpu-model
//   Discontinued models (e.g., Core 2, Nehalem, and Westmere) are not present
//   in the table; in those cases, we assume vulnerability by default, unless
//   otherwise mentions.
//
// * AMD: https://www.amd.com/en/corporate/product-security
//
// Further pessimistically, we default to assigning vulnerability in the case
// unknown architectures.

namespace arch {

// Whether the CPU is susceptible to swapgs speculation attacks:
// https://software.intel.com/security-software-guidance/advisory-guidance/speculative-behavior-swapgs-and-segment-registers
//
// CVE-2019-1125.
template <typename CpuidIoProvider>
inline bool HasX86SwapgsBug(CpuidIoProvider&& cpuid) {
  switch (arch::GetVendor(cpuid)) {
    case arch::Vendor::kUnknown:
    // All Intel CPUs seem to be affected and there is no indication that they
    // intend to fix this.
    case arch::Vendor::kIntel:
      return true;
    case arch::Vendor::kAmd:
      return false;
  }
  return false;
}

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_BUG_H_
