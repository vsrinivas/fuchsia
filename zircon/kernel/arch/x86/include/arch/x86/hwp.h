// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_HWP_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_HWP_H_

#include <arch/x86/cpuid.h>
#include <arch/x86/platform_access.h>
#include <ktl/optional.h>
#include <ktl/string_view.h>

namespace x86 {

enum class IntelHwpPolicy {
  // Use BIOS-specified settings if available, falling back to balanced.
  kBiosSpecified,

  // Use high performance, balanaced, or low-power policies respectively.
  kPerformance,
  kBalanced,
  kPowerSave,

  // Use settings that give predictable performance, such as is required
  // for benchmarking.
  kStablePerformance,
};

// Parse a string as an HWP policy.
ktl::optional<IntelHwpPolicy> IntelHwpParsePolicy(const char* str);

// Initialise the Intel HWP on the current CPU.
//
// If HWP is not supported on the current CPU, no action will be taken.
void IntelHwpInit(const cpu_id::CpuId*, MsrAccess*, IntelHwpPolicy);

// Determine if Intel HWP is supported on given CPU.
bool IntelHwpSupported(const cpu_id::CpuId* cpuid);

}  // namespace x86

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_HWP_H_
