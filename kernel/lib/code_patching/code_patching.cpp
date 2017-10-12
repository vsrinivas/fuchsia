// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/ops.h>
#include <lib/code_patching.h>
#include <lk/init.h>

extern const CodePatchInfo __start_code_patch_table[];
extern const CodePatchInfo __stop_code_patch_table[];

static void apply_startup_code_patches(uint level) {
    for (const CodePatchInfo* patch = __start_code_patch_table;
         patch < __stop_code_patch_table; ++patch) {
        patch->apply_func(patch);
        arch_sync_cache_range((addr_t)patch->dest_addr, patch->dest_size);
    }
}

LK_INIT_HOOK(code_patching, apply_startup_code_patches,
             LK_INIT_LEVEL_ARCH_EARLY);
