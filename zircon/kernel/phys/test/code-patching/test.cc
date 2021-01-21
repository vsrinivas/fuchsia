// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "test.h"

#include <lib/arch/self-modification.h>
#include <lib/code-patching/code-patching.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/view.h>
#include <stdio.h>

#include <ktl/byte.h>
#include <ktl/span.h>

#include "../test-main.h"

const char Symbolize::kProgramName_[] = "code-patching-test";

// Defined in add-one.S.
extern "C" uint64_t AddOne(uint64_t x);

namespace {

// Returns the address range within this executable associated with a given
// link-time, virtual range.
fbl::Span<ktl::byte> GetInstructionRange(uint64_t range_start, size_t range_size) {
// If we are not static PIE, then we expect link-time addresses to already
// be offset from PHYS_LOAD_ADDRESS; in that case, adjust.
#ifndef ZX_STATIC_PIE
  range_start -= reinterpret_cast<uint64_t>(PHYS_LOAD_ADDRESS);
#endif

  auto* loaded_start = reinterpret_cast<ktl::byte*>(const_cast<char*>(PHYS_LOAD_ADDRESS));
  const size_t loaded_size = static_cast<size_t>(_end - PHYS_LOAD_ADDRESS);
  fbl::Span<ktl::byte> loaded_range{loaded_start, loaded_size};
  ZX_ASSERT(range_size <= loaded_range.size());
  ZX_ASSERT(range_start <= loaded_range.size() - range_size);
  return loaded_range.subspan(range_start, range_size);
}

}  // namespace

int TestMain(void* zbi_ptr, arch::EarlyTicks) {
  zbitl::View<ktl::span<ktl::byte>> zbi({static_cast<ktl::byte*>(zbi_ptr), SIZE_MAX});

  ktl::span<ktl::byte> raw_patches;
  for (auto [header, payload] : zbi) {
    // The patch metadata is expected to be stored in an uncompressed ramdisk
    // item.
    if (header->type == ZBI_TYPE_STORAGE_RAMDISK &&
        (header->flags & ZBI_FLAG_STORAGE_COMPRESSED) == 0) {
      raw_patches = payload;
      break;
    }
  }
  if (auto result = zbi.take_error(); result.is_error()) {
    zbitl::PrintViewError(result.error_value());
    return 1;
  }

  if (raw_patches.size() % sizeof(code_patching::Directive)) {
    printf("Expected total size of code patch directives to be a multiple of %lu: got %zu\n",
           sizeof(code_patching::Directive), raw_patches.size());
    return 1;
  }

  ktl::span<code_patching::Directive> patches{
      reinterpret_cast<code_patching::Directive*>(raw_patches.data()),
      raw_patches.size() / sizeof(code_patching::Directive),
  };

  printf("Patches found:\n");
  printf("| %-4s | %-8s | %-8s | %-4s |\n", "ID", "Start", "End", "Size");
  for (const auto& patch : patches) {
    printf("| %-4u | %#-8lx | %#-8lx | %-4u |\n", patch.id, patch.range_start,
           patch.range_start + patch.range_size, patch.range_size);
  }

  if (patches.size() != 1) {
    printf("Expected 1 code patch directive: got %zu", patches.size());
    return 1;
  }

  if (patches[0].id != kAddOneCaseId) {
    printf("Expected a patch case ID of %u: got %u", kAddOneCaseId, patches[0].id);
    return 1;
  } else if (patches[0].range_size != kAddOnePatchSize) {
    printf("Expected patch case #%u to cover %zu bytes; got %u", kAddOneCaseId, kAddOnePatchSize,
           patches[0].range_size);
    return 1;
  }

  if (uint64_t result = AddOne(583); result != 584) {
    printf("AddOne(583) returned %lu; expected 584.\n", result);
    return 1;
  }

  // After patching (and synchronizing the instruction and data caches), we
  // expect AddOne() to be the identity function.
  code_patching::NopFill(GetInstructionRange(patches[0].range_start, patches[0].range_size));
  arch::PostSelfModificationCacheSync();

  if (uint64_t result = AddOne(583); result != 583) {
    printf("Patched AddOne(583) returned %lu; expected 583.\n", result);
    return 1;
  }

  return 0;
}
