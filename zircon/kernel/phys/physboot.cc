// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/boot-options/boot-options.h>
#include <lib/code-patching/code-patches.h>
#include <lib/code-patching/code-patching.h>
#include <lib/memalloc/range.h>
#include <lib/zbitl/error_stdio.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <ktl/array.h>
#include <ktl/move.h>
#include <phys/allocation.h>
#include <phys/boot-zbi.h>
#include <phys/handoff.h>
#include <phys/kernel-package.h>
#include <phys/main.h>
#include <phys/symbolize.h>

#ifdef __x86_64__
#include "trampoline-boot.h"

using ChainBoot = TrampolineBoot;

#else

using ChainBoot = BootZbi;

#endif

#include <ktl/enforce.h>

const char Symbolize::kProgramName_[] = "physboot";

namespace {

PhysBootTimes gBootTimes;

// A guess about the upper bound on reserve_memory_size so we can do a single
// allocation before decoding the header and probably not need to relocate.
constexpr uint64_t kKernelBssEstimate = 1024 * 1024 * 2;

ChainBoot LoadZirconZbi(KernelStorage::Bootfs kernelfs) {
  // Now we select our kernel ZBI.
  auto it = kernelfs.find({kDefaultKernelPackage, kKernelZbiName});
  if (auto result = kernelfs.take_error(); result.is_error()) {
    printf("physboot: Error in looking for kernel ZBI within STORAGE_KERNEL item: ");
    zbitl::PrintBootfsError(result.error_value());
    abort();
  }
  if (it == kernelfs.end()) {
    printf("physboot: Could not find kernel ZBI (%.*s/%.*s) within STORAGE_KERNEL item\n",
           static_cast<int>(kDefaultKernelPackage.size()), kDefaultKernelPackage.data(),
           static_cast<int>(kKernelZbiName.size()), kKernelZbiName.data());
    abort();
  }
  ktl::span<ktl::byte> kernel_bytes = {const_cast<ktl::byte*>(it->data.data()), it->data.size()};

  // Patch the kernel image in the BOOTFS in place before loading it.
  code_patching::Patcher patcher;
  if (auto result = patcher.Init(ktl::move(kernelfs), kDefaultKernelPackage); result.is_error()) {
    printf("physboot: Failed to initialize code patching: ");
    code_patching::PrintPatcherError(result.error_value());
    abort();
  }
  ArchPatchCode(ktl::move(patcher), kernel_bytes, KERNEL_LINK_ADDRESS);

  BootZbi::InputZbi kernel_zbi(kernel_bytes);
  ChainBoot boot;
  if (auto result = boot.Init(kernel_zbi); result.is_error()) {
    printf("physboot: Cannot read STORAGE_KERNEL item ZBI: ");
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  if (auto result = boot.Load(kKernelBssEstimate); result.is_error()) {
    printf("physboot: Cannot load decompressed kernel: ");
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  return boot;
}

[[noreturn]] void BootZircon(KernelStorage kernel_storage) {
  ChainBoot boot = LoadZirconZbi(kernel_storage.GetBootfs());

  // Repurpose the storage item as a place to put the handoff payload.
  KernelStorage::Zbi::iterator handoff_item = kernel_storage.item();

  // `boot`'s data ZBI at this point is the tail of the decompressed kernel
  // ZBI; overwrite that with the original data ZBI.
  boot.DataZbi().storage() = {
      const_cast<ktl::byte*>(kernel_storage.zbi().storage().data()),
      kernel_storage.zbi().storage().size(),
  };

  Allocation relocated_zbi;
  if (boot.MustRelocateDataZbi()) {
    // Actually, the original data ZBI must be moved elsewhere since it
    // overlaps the space where the fixed-address kernel will be loaded.
    fbl::AllocChecker ac;
    relocated_zbi =
        Allocation::New(ac, memalloc::Type::kDataZbi, kernel_storage.zbi().storage().size(),
                        arch::kZbiBootDataAlignment);
    if (!ac.check()) {
      printf("physboot: Cannot allocate %#zx bytes aligned to %#zx for relocated data ZBI!\n",
             kernel_storage.zbi().storage().size(), arch::kZbiBootDataAlignment);
      abort();
    }
    if (auto result = kernel_storage.zbi().Copy(relocated_zbi.data(), kernel_storage.zbi().begin(),
                                                kernel_storage.zbi().end());
        result.is_error()) {
      kernel_storage.zbi().ignore_error();
      printf("physboot: Failed to relocate data ZBI: ");
      zbitl::PrintViewCopyError(result.error_value());
      printf("\n");
      abort();
    }
    ZX_ASSERT(kernel_storage.zbi().take_error().is_ok());

    // Rediscover the handoff item's new location in memory.
    ChainBoot::Zbi relocated_image(relocated_zbi.data());
    auto it = relocated_image.begin();
    while (it != relocated_image.end() && it.item_offset() < handoff_item.item_offset()) {
      ++it;
    }
    ZX_ASSERT(it != relocated_image.end());
    ZX_ASSERT(relocated_image.take_error().is_ok());

    boot.DataZbi() = ktl::move(relocated_image);
    handoff_item = it;
  }

  // Initialize the handoff payload.
  auto handoff_payload = handoff_item->payload;
  ZX_ASSERT(handoff_payload.size() >= sizeof(PhysHandoff));
  static_assert(alignof(PhysHandoff) <= ZBI_ALIGNMENT);

  auto handoff = new (handoff_payload.data()) PhysHandoff;

  // TODO(fxbug.dev/32414): There are no time samples taken in physboot after
  // the decompression is done, since it's not doing much else yet.  The first
  // sample taken by the kernel proper measures the interval containing all of
  // physboot's "handoff" work.  Additional time samples for more substantial
  // stages of pre-handoff setup work will be added as physboot starts doing
  // more work for the kernel.
  handoff->times = gBootTimes;

  handoff->zbi = reinterpret_cast<uintptr_t>(boot.DataZbi().storage().data());

  // Even though the kernel is still a ZBI and mostly using the ZBI protocol
  // for booting, the PhysHandoff pointer (physical address) is now the
  // argument to the kernel, not the data ZBI address.
  boot.Boot(handoff);
}

}  // namespace

void ZbiMain(void* zbi_ptr, arch::EarlyTicks ticks) {
  if (gBootOptions->phys_verbose) {
    Symbolize::GetInstance()->Context();
  }

  InitMemory(zbi_ptr);

  gBootTimes.Set(PhysBootTimes::kZbiEntry, ticks);

  // This marks the interval between handoff from the boot loader (kZbiEntry)
  // and phys environment setup with identity-mapped memory management et al.
  gBootTimes.SampleNow(PhysBootTimes::kPhysSetup);

  auto zbi_header = static_cast<zbi_header_t*>(zbi_ptr);
  auto zbi = zbitl::StorageFromRawHeader<ktl::span<ktl::byte>>(zbi_header);

  // Unpack the compressed KERNEL_STORAGE payload.
  KernelStorage kernel_storage;
  kernel_storage.Init(zbitl::View{zbi});
  kernel_storage.GetTimes(gBootTimes);

  // TODO(mcgrathr): Bloat the binary so the total kernel.zbi size doesn't
  // get too comfortably small while physboot functionality is still growing.
  static const ktl::array<char, 512 * 1024> kPad{1};
  __asm__ volatile("" ::"m"(kPad), "r"(kPad.data()));

  BootZircon(ktl::move(kernel_storage));
}
