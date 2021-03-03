// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/code-patching/code-patches.h>
#include <zircon/assert.h>

#include <cstdint>
#include <cstdio>

#include <arch/code-patches/case-id.h>

namespace {

// TODO(68585): While .code-patches is allocated and accessed from directly
// within the kernel, we expect its recorded addresses to be the final,
// link-time ones.
ktl::span<ktl::byte> GetInstructions(uint64_t range_start, size_t range_size) {
  return {reinterpret_cast<ktl::byte*>(range_start), range_size};
}

}  // namespace

// Declared in <lib/code-patching/code-patches.h>.
void ArchPatchCode(ktl::span<const code_patching::Directive> patches) {
  for (const code_patching::Directive& patch : patches) {
    ktl::span<ktl::byte> insns = GetInstructions(patch.range_start, patch.range_size);
    if (insns.empty()) {
      ZX_PANIC("code-patching: unrecognized address range for patch case ID %u: [%#lx, %#lx)",
               patch.id, patch.range_start, patch.range_start + patch.range_size);
    }

    switch (patch.id) {
      default:
        ZX_PANIC("code-patching: unrecognized patch case ID: %u: [%#lx, %#lx)\n", patch.id,
                 patch.range_start, patch.range_start + patch.range_size);
    }
  }
}
