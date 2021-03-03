// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_CODE_PATCHES_INCLUDE_ARCH_CODE_PATCHES_CASE_ID_H_
#define ZIRCON_KERNEL_ARCH_X86_CODE_PATCHES_INCLUDE_ARCH_CODE_PATCHES_CASE_ID_H_

// Defines x86 code patching case IDs. IDs corresponding to the cases
// involving the wholesale patching of a specific function are expected to be
// defined as `CASE_ID_${NAME}`, where `${NAME}` is the upper-cased version of
// the function name.

// Addresses `swapgs` speculation attacks (CVE-2019-1125):
// https://software.intel.com/security-software-guidance/advisory-guidance/speculative-behavior-swapgs-and-segment-registers
// Mitigation involves following `swapgs` instances with a load fence;
// mitigation is the default and patching is equivalent to `nop`-ing it out.
#define CASE_ID_SWAPGS_MITIGATION 0

#endif  // ZIRCON_KERNEL_ARCH_X86_CODE_PATCHES_INCLUDE_ARCH_CODE_PATCHES_CASE_ID_H_
