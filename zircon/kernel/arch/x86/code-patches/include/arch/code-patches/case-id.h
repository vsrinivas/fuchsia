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

// This case serves as a verification that code-patching was performed before
// the kernel was booted, `nop`ing out a trap among the kernel's earliest
// instructions.
#define CASE_ID_SELF_TEST 0

// Addresses `swapgs` speculation attacks (CVE-2019-1125):
// https://software.intel.com/security-software-guidance/advisory-guidance/speculative-behavior-swapgs-and-segment-registers
// Mitigation involves following `swapgs` instances with a load fence;
// mitigation is the default and patching is equivalent to `nop`-ing it out.
#define CASE_ID_SWAPGS_MITIGATION 1

// Addresses MDS and TAA vulnerabilities (CVE-2018-12126, CVE-2018-12127,
// CVE-2018-12130, CVE-2019-11091, and CVE-2019-11135):
// https://www.intel.com/content/www/us/en/architecture-and-technology/mds.html
//
// Mitigation involves making use of the MD_CLEAR feature, when available;
// mitigation is the default and patching is equivalent to `nop`-ing it out.
#define CASE_ID_MDS_TAA_MITIGATION 2

// Encodes a decision between implementations of
// `_x86_user_copy_to_or_from_user()`, in which we try to take advantage of
// optimizations (e.g., in the case when `movsb` is expected to be more
// efficient than `movsq`) and securities (e.g., SMAP) when available.
//
// Note: the "__" is intentional as the function name has a leading underscore.
#define CASE_ID__X86_COPY_TO_OR_FROM_USER 3

// Addresses Branch Target Injection / Spectre Variant 2 attacks
// (CVE-2017-5715) by "retpolines":
// https://software.intel.com/security-software-guidance/advisory-guidance/branch-target-injection
//
// Note: the "___" is intentional as the function name has two leading
// underscores.
#define CASE_ID___X86_INDIRECT_THUNK_R11 4

// Relates to the optimizations available for C string utilities.
//
// Note: the "___" is intentional as the function name has two leading
// underscores.
#define CASE_ID___UNSANITIZED_MEMCPY 5
#define CASE_ID___UNSANITIZED_MEMSET 6

#endif  // ZIRCON_KERNEL_ARCH_X86_CODE_PATCHES_INCLUDE_ARCH_CODE_PATCHES_CASE_ID_H_
