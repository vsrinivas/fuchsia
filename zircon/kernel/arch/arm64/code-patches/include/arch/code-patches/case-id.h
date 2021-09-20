// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_CODE_PATCHES_INCLUDE_ARCH_CODE_PATCHES_CASE_ID_H_
#define ZIRCON_KERNEL_ARCH_ARM64_CODE_PATCHES_INCLUDE_ARCH_CODE_PATCHES_CASE_ID_H_

// Defines arm64 code patching case IDs. IDs corresponding to the cases
// involving the wholesale patching of a specific function are expected to be
// defined as `CASE_ID_${NAME}`, where `${NAME}` is the upper-cased version of
// the function name.

// This case serves as a verification that code-patching was performed before
// the kernel was booted, `nop`ing out a trap among the kernel's earliest
// instructions.
#define CASE_ID_SELF_TEST 0

#endif  // ZIRCON_KERNEL_ARCH_ARM64_CODE_PATCHES_INCLUDE_ARCH_CODE_PATCHES_CASE_ID_H_
