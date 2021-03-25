// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/code-patching/code-patches.h>

#include <lk/init.h>

// TODO(68585): While v2 code-patching remains in the kernel, the .code-patches
// section will be allocated and the directives within can be accessed directly.
// (In physboot, this will be accessed via a STORAGE_KERNEL item.)
extern "C" const code_patching::Directive __start_code_patches[];
extern "C" const code_patching::Directive __stop_code_patches[];

namespace {

ktl::span<const code_patching::Directive> GetPatchDirectives() {
  return {__start_code_patches, static_cast<size_t>(__stop_code_patches - __start_code_patches)};
}

void ApplyPatches(uint level) { ArchPatchCode(GetPatchDirectives()); }

}  // namespace

LK_INIT_HOOK(code_patching, ApplyPatches, LK_INIT_LEVEL_PLATFORM_PREVM)
