// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_CODE_PATCHING_INCLUDE_LIB_CODE_PATCHING_CODE_PATCHES_H_
#define ZIRCON_KERNEL_LIB_CODE_PATCHING_INCLUDE_LIB_CODE_PATCHING_CODE_PATCHES_H_

#include <lib/code-patching/code-patching.h>

#include <ktl/byte.h>
#include <ktl/span.h>

// Performs code patching for associated provided directives, according to the
// case IDs documented in <arch/code-patches/case-id.h>. Declared here, but
// defined in //zircon/kernel/arch/$cpu/code-patching.
void ArchPatchCode(code_patching::Patcher patcher, ktl::span<ktl::byte> patchee,
                   uint64_t patchee_load_bias);

#endif  // ZIRCON_KERNEL_LIB_CODE_PATCHING_INCLUDE_LIB_CODE_PATCHING_CODE_PATCHES_H_
