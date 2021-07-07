// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "test.h"

#include <lib/arch/cache.h>
#include <lib/code-patching/code-patching.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/view.h>
#include <stdio.h>

#include <ktl/byte.h>
#include <ktl/span.h>
#include <ktl/string_view.h>

#include "../test-main.h"

const char Symbolize::kProgramName_[] = "code-patching-test";

// Defined in add-one.S.
extern "C" uint64_t AddOne(uint64_t x);

// Defined by //zircon/kernel/phys/test/code-patching:multiply_by_factor.
extern "C" uint64_t multiply_by_factor(uint64_t x);

namespace {

constexpr size_t kExpectedNumPatches = 2;

// Returns the address range within this executable associated with a given
// link-time, virtual range.
ktl::span<ktl::byte> GetInstructionRange(uint64_t range_start, size_t range_size) {
  ZX_ASSERT(range_start > kLinkTimeLoadAddress);
  range_start -= kLinkTimeLoadAddress;

  auto* loaded_start = reinterpret_cast<ktl::byte*>(const_cast<char*>(PHYS_LOAD_ADDRESS));
  const size_t loaded_size = static_cast<size_t>(_end - PHYS_LOAD_ADDRESS);
  ktl::span<ktl::byte> loaded_range{loaded_start, loaded_size};
  ZX_ASSERT(range_size <= loaded_range.size());
  ZX_ASSERT(range_start <= loaded_range.size() - range_size);
  return loaded_range.subspan(range_start, range_size);
}

int TestAddOnePatching(const code_patching::Directive& patch) {
  ZX_ASSERT_MSG(patch.range_size == kAddOnePatchSize,
                "Expected patch case #%u to cover %zu bytes; got %u", kAddOneCaseId,
                kAddOnePatchSize, patch.range_size);

  {
    uint64_t result = AddOne(583);
    ZX_ASSERT_MSG(result == 584, "AddOne(583) returned %lu; expected 584.\n", result);
  }

  // After patching (and synchronizing the instruction and data caches), we
  // expect AddOne() to be the identity function.
  auto insns = GetInstructionRange(patch.range_start, patch.range_size);
  code_patching::NopFill(insns);
  arch::GlobalCacheConsistencyContext().SyncRange(patch.range_start, patch.range_size);
  {
    uint64_t result = AddOne(583);
    ZX_ASSERT_MSG(result == 583, "Patched AddOne(583) returned %lu; expected 583.\n", result);
  }
  return 0;
}

int TestMultiplyByFactorPatching(const code_patching::Directive& patch) {
  ZX_ASSERT_MSG(patch.range_size == kMultiplyByFactorPatchSize,
                "Expected patch case #%u to cover %zu bytes; got %u", kMultiplyByFactorCaseId,
                kMultiplyByFactorPatchSize, patch.range_size);

  auto insns = GetInstructionRange(patch.range_start, patch.range_size);

  // After patching and synchronizing, we expect multiply_by_factor() to
  // multiply by 2.
  ktl::span<const ktl::byte> multiply_by_two = GetPatchAlternative("multiply_by_two");
  code_patching::Patch(insns, multiply_by_two);
  arch::GlobalCacheConsistencyContext().SyncRange(patch.range_start, patch.range_size);

  {
    uint64_t result = multiply_by_factor(583);
    ZX_ASSERT_MSG(result == 2 * 583, "multiply_by_factor(583) returned %lu; expected %d.\n", result,
                  2 * 583);
  }

  // After patching and synchronizing, we expect multiply_by_factor() to
  // multiply by ten.
  ktl::span<const ktl::byte> multiply_by_ten = GetPatchAlternative("multiply_by_ten");
  code_patching::Patch(insns, multiply_by_ten);
  arch::GlobalCacheConsistencyContext().SyncRange(patch.range_start, patch.range_size);
  {
    uint64_t result = multiply_by_factor(583);
    ZX_ASSERT_MSG(result == 10 * 583, "multiply_by_factor(583) returned %lu; expected %d.\n",
                  result, 10 * 583);
  }

  return 0;
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

  if (patches.size() != kExpectedNumPatches) {
    printf("Expected %zu code patch directive: got %zu", kExpectedNumPatches, patches.size());
    return 1;
  }

  for (size_t i = 0; i < patches.size(); ++i) {
    const auto& patch = patches[i];
    switch (patch.id) {
      case kAddOneCaseId:
        if (int result = TestAddOnePatching(patch); result != 0) {
          return result;
        }
        break;
      case kMultiplyByFactorCaseId:
        if (int result = TestMultiplyByFactorPatching(patch); result != 0) {
          return result;
        }
        break;
      default:
        printf("Unexpected patch case ID: %u\n", patch.id);
        return 1;
    }
  }

  return 0;
}
