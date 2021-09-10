// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "test.h"

#include <lib/arch/cache.h>
#include <lib/code-patching/code-patching.h>
#include <lib/memalloc/range.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/items/bootfs.h>
#include <lib/zbitl/view.h>
#include <stdio.h>

#include <fbl/alloc_checker.h>
#include <ktl/byte.h>
#include <ktl/span.h>
#include <ktl/string_view.h>
#include <phys/zbitl-allocation.h>

#include "../test-main.h"

const char Symbolize::kProgramName_[] = "code-patching-test";

// The kernel package under which code patching blobs live.
constexpr ktl::string_view kPackage = "code-patches-test";

// The file within the kernel package containing the code-patch metadata.
constexpr ktl::string_view kPatchesBin = "code-patches.bin";

// The namespace within the kernel package under which the patche alternatives
// are found.
constexpr ktl::string_view kPatchAlternativeDir = "code-patches";

// Defined in add-one.S.
extern "C" uint64_t AddOne(uint64_t x);

// Defined by //zircon/kernel/phys/test/code-patching:multiply_by_factor.
extern "C" uint64_t multiply_by_factor(uint64_t x);

namespace {

using BootfsView = zbitl::BootfsView<ktl::span<const ktl::byte>>;

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

void SyncInstructionRange(ktl::span<ktl::byte> insns) {
  arch::GlobalCacheConsistencyContext().SyncRange(reinterpret_cast<uint64_t>(insns.data()),
                                                  insns.size());
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
  SyncInstructionRange(insns);
  {
    uint64_t result = AddOne(583);
    ZX_ASSERT_MSG(result == 583, "Patched AddOne(583) returned %lu; expected 583.\n", result);
  }
  return 0;
}

int TestMultiplyByFactorPatching(BootfsView& bootfs, const code_patching::Directive& patch) {
  ZX_ASSERT_MSG(patch.range_size == kMultiplyByFactorPatchSize,
                "Expected patch case #%u to cover %zu bytes; got %u", kMultiplyByFactorCaseId,
                kMultiplyByFactorPatchSize, patch.range_size);

  auto insns = GetInstructionRange(patch.range_start, patch.range_size);

  // After patching and synchronizing, we expect multiply_by_factor() to
  // multiply by 2.
  auto it = bootfs.find({kPackage, kPatchAlternativeDir, "multiply_by_two"});
  if (auto result = bootfs.take_error(); result.is_error()) {
    printf("FAILED: Error in looking for the mutiply_by_two patch alternative: ");
    zbitl::PrintBootfsError(result.error_value());
    return 1;
  }
  if (it == bootfs.end()) {
    printf("FAILED: Could not find \"%.*s/%.*s/multiply_by_two\" within BOOTFS\n",
           static_cast<int>(kPackage.size()), kPackage.data(),
           static_cast<int>(kPatchAlternativeDir.size()), kPatchAlternativeDir.data());
    return 1;
  }
  code_patching::Patch(insns, it->data);
  SyncInstructionRange(insns);

  {
    uint64_t result = multiply_by_factor(583);
    ZX_ASSERT_MSG(result == 2 * 583, "multiply_by_factor(583) returned %lu; expected %d.\n", result,
                  2 * 583);
  }

  // After patching and synchronizing, we expect multiply_by_factor() to
  // multiply by ten.
  it = bootfs.find({kPackage, kPatchAlternativeDir, "multiply_by_ten"});
  if (auto result = bootfs.take_error(); result.is_error()) {
    printf("FAILED: Error in looking for the mutiply_by_ten patch alternative: ");
    zbitl::PrintBootfsError(result.error_value());
    return 1;
  }
  if (it == bootfs.end()) {
    printf("physboot: Could not find \"%.*s/%.*s/multiply_by_ten\" within BOOTFS\n",
           static_cast<int>(kPackage.size()), kPackage.data(),
           static_cast<int>(kPatchAlternativeDir.size()), kPatchAlternativeDir.data());
    return 1;
  }
  code_patching::Patch(insns, it->data);
  SyncInstructionRange(insns);

  {
    uint64_t result = multiply_by_factor(583);
    ZX_ASSERT_MSG(result == 10 * 583, "multiply_by_factor(583) returned %lu; expected %d.\n",
                  result, 10 * 583);
  }

  return 0;
}

}  // namespace

int TestMain(void* zbi_ptr, arch::EarlyTicks) {
  // Initialize memory for allocation/free.
  InitMemory(zbi_ptr);

  zbitl::View zbi(zbitl::StorageFromRawHeader(static_cast<const zbi_header_t*>(zbi_ptr)));

  // Search for a payload of type ZBI_TYPE_STORAGE_KERNEL
  auto zbi_it = zbi.begin();
  while (zbi_it != zbi.end() && zbi_it->header->type != ZBI_TYPE_STORAGE_KERNEL) {
    ++zbi_it;
  }

  // Ensure there was no error during iteration.
  if (auto result = zbi.take_error(); result.is_error()) {
    printf("FAILED: Error while enumerating ZBI: ");
    zbitl::PrintViewError(result.error_value());
    return 1;
  }

  // Fail if we didn't find anything.
  if (zbi_it == zbi.end()) {
    printf("FAILED: No STORAGE_KERNEL item found.\n");
    return 1;
  }

  fbl::AllocChecker ac;
  const uint32_t bootfs_size = zbitl::UncompressedLength(*(zbi_it->header));
  auto bootfs_buffer = Allocation::New(ac, memalloc::Type::kKernelStorage, bootfs_size);
  if (!ac.check()) {
    printf("FAILED: Cannot allocate %#x bytes for decompressed STORAGE_KERNEL item!\n",
           bootfs_size);
    abort();
  }

  if (auto result = zbi.CopyStorageItem(bootfs_buffer.data(), zbi_it, ZbitlScratchAllocator);
      result.is_error()) {
    printf("FAILED: Cannot load STORAGE_KERNEL item (uncompressed size %#x): ", bootfs_size);
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  BootfsView bootfs;
  if (auto result = BootfsView::Create(bootfs_buffer.data()); result.is_error()) {
    zbitl::PrintBootfsError(result.error_value());
    return 1;
  } else {
    bootfs = std::move(result.value());
  }

  auto bootfs_it = bootfs.find({kPackage, kPatchesBin});
  if (auto result = bootfs.take_error(); result.is_error()) {
    printf("FAILED: Error in looking for code patching metadata: ");
    zbitl::PrintBootfsError(result.error_value());
    return 1;
  }
  if (bootfs_it == bootfs.end()) {
    printf("FAILED: Could not find \"/%.*s/%.*s\" within BOOTFS\n",
           static_cast<int>(kPackage.size()), kPackage.data(), static_cast<int>(kPatchesBin.size()),
           kPatchesBin.data());
    return 1;
  }

  if (bootfs_it->data.size() % sizeof(code_patching::Directive)) {
    printf("Expected total size of code patch directives to be a multiple of %lu: got %zu\n",
           sizeof(code_patching::Directive), bootfs_it->data.size());
    return 1;
  }

  ktl::span<const code_patching::Directive> patches = {
      reinterpret_cast<const code_patching::Directive*>(bootfs_it->data.data()),
      bootfs_it->data.size() / sizeof(code_patching::Directive),
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
        if (int result = TestMultiplyByFactorPatching(bootfs, patch); result != 0) {
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
