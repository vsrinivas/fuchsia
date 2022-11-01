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
#include <phys/symbolize.h>

namespace {

void PrintCaseInfo(const code_patching::Directive& patch, const char* fmt, ...) {
  printf("%s: code-patching: ", ProgramName());
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  printf(": [%#lx, %#lx)\n", patch.range_start, patch.range_start + patch.range_size);
}

}  // namespace

// Declared in <lib/code-patching/code-patches.h>.
void ArchPatchCode(code_patching::Patcher patcher, ktl::span<ktl::byte> patchee,
                   uint64_t patchee_load_bias) {
  bool performed = false;
  for (const code_patching::Directive& patch : patcher.patches()) {
    ZX_ASSERT(patch.range_start >= patchee_load_bias);
    ZX_ASSERT(patch.range_size <= patchee.size());
    ZX_ASSERT(patchee.size() - patch.range_size >= patch.range_start - patchee_load_bias);

    ktl::span<ktl::byte> insns =
        patchee.subspan(patch.range_start - patchee_load_bias, patch.range_size);

    switch (patch.id) {
      case CASE_ID_SELF_TEST:
        patcher.NopFill(insns);
        PrintCaseInfo(patch, "'smoke test' trap patched");
        performed = true;
        break;
      default:
        ZX_PANIC("%s: code-patching: unrecognized patch case ID: %u: [%#lx, %#lx)", ProgramName(),
                 patch.id, patch.range_start, patch.range_start + patch.range_size);
    }
  }
  if (!performed) {
    ZX_PANIC("%s: code-patching: failed to patch the kernel", ProgramName());
  }
}
