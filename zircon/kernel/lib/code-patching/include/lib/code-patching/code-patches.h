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
#include <ktl/string_view.h>

// This file is concerned with the use of the code-patching facilities
// ("code-patches" as opposed "code-patching").

// TODO(68585): Defined for now by //zircon/kernel:code-patching-embedding,
// but eventually these blobs will be accessed via a STORAGE_KERNEL item.
ktl::span<const ktl::byte> GetPatchAlternative(ktl::string_view name);

// Performs code patching for the provided directives, according to the case IDs
// documented in <arch/code-patches/case-id.h>. Declared here, but defined in
// //zircon/kernel/arch/$cpu/code-patching.
void ArchPatchCode(ktl::span<const code_patching::Directive> patches);

#endif  // ZIRCON_KERNEL_LIB_CODE_PATCHING_INCLUDE_LIB_CODE_PATCHING_CODE_PATCHES_H_
