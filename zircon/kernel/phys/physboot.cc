// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/arch/zbi-boot.h>
#include <lib/boot-options/boot-options.h>
#include <lib/boot-options/word-view.h>
#include <lib/memalloc/range.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <ktl/array.h>
#include <ktl/optional.h>
#include <phys/allocation.h>
#include <phys/boot-zbi.h>
#include <phys/handoff.h>
#include <phys/main.h>
#include <phys/stdio.h>
#include <phys/symbolize.h>
#include <phys/zbitl-allocation.h>
#include <pretty/cpp/sizes.h>

#ifdef __x86_64__
#include "trampoline-boot.h"

using ChainBoot = TrampolineBoot;

#else

using ChainBoot = BootZbi;

#endif

const char Symbolize::kProgramName_[] = "physboot";

namespace {

PhysBootTimes gBootTimes;

// A guess about the upper bound on reserve_memory_size so we can do a single
// allocation before decoding the header and probably not need to relocate.
constexpr uint64_t kKernelBssEstimate = 1024 * 1024 * 2;

struct LoadedZircon {
  Allocation buffer;
  ChainBoot boot;
};

LoadedZircon LoadZircon(BootZbi::InputZbi& zbi, BootZbi::InputZbi::iterator kernel_item,
                        uint64_t reserve_memory_estimate) {
  fbl::AllocChecker ac;
  uint32_t kernel_length = zbitl::UncompressedLength(*kernel_item->header);
  BootZbi::Size buffer_sizes = BootZbi::SuggestedAllocation(kernel_length);

  // That covers the uncompressed size of the image alone.  Preallocate enough
  // space after it that the bss and boot_alloc reserve are likely to fit.
  buffer_sizes.size += reserve_memory_estimate + BootZbi::kKernelBootAllocReserve;

  auto buffer =
      Allocation::New(ac, memalloc::Type::kKernel, buffer_sizes.size, buffer_sizes.alignment);
  if (!ac.check()) {
    printf(
        "physboot: Cannot allocate %#zx bytes aligned to %#zx for decompressed kernel payload!\n",
        buffer_sizes.size, buffer_sizes.alignment);
    abort();
  }

  // This marks the interval from completing basic phys environment setup
  // (kPhysSetup) to when the ZBI has been decoded enough to start accessing
  // the real kernel payload (which is usually compressed).
  gBootTimes.SampleNow(PhysBootTimes::kDecompressStart);

  if (auto result = zbi.CopyStorageItem(buffer.data(), kernel_item, ZbitlScratchAllocator);
      result.is_error()) {
    printf("physboot: Cannot load STORAGE_KERNEL item (uncompressed size %#x): ", kernel_length);
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  // This marks just the decompression (or copying) time.
  gBootTimes.SampleNow(PhysBootTimes::kDecompressEnd);

  BootZbi::InputZbi kernel_zbi(zbitl::AsBytes(buffer.data()));

  {
    debugf("physboot: STORAGE_KERNEL decompressed %s -> %s\n",
           pretty::FormattedBytes(kernel_item->header->length).c_str(),
           pretty::FormattedBytes(kernel_zbi.size_bytes()).c_str());
  }

  ChainBoot boot;
  if (auto result = boot.Init(kernel_zbi); result.is_error()) {
    printf("physboot: Cannot read STORAGE_KERNEL item ZBI: ");
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  if (auto result = boot.Load(reserve_memory_estimate); result.is_error()) {
    printf("physboot: Cannot load decompressed kernel: ");
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  return {std::move(buffer), std::move(boot)};
}

[[noreturn]] void BootZircon(BootZbi::InputZbi& zbi, BootZbi::InputZbi::iterator kernel_item) {
  // While `buffer` owns the allocation of the decompressed STORAGE_KERNEL
  // payload, it may no longer own the kernel image memory `boot` points to,
  // since BootZbi::Load() handles relocation internally.
  auto [buffer, boot] = LoadZircon(zbi, kernel_item, kKernelBssEstimate);

  // `boot`'s data ZBI at this point is the tail of the decompressed kernel
  // ZBI; overwrite that with the original data ZBI.
  boot.DataZbi().storage() = {
      const_cast<std::byte*>(zbi.storage().data()),
      zbi.storage().size(),
  };

  Allocation relocated_zbi;
  if (boot.MustRelocateDataZbi()) {
    // Actually, the original data ZBI must be moved elsewhere since it
    // overlaps the space where the fixed-address kernel will be loaded.
    fbl::AllocChecker ac;
    relocated_zbi = Allocation::New(ac, memalloc::Type::kDataZbi, zbi.storage().size(),
                                    arch::kZbiBootDataAlignment);
    if (!ac.check()) {
      printf("physboot: Cannot allocate %#zx bytes aligned to %#zx for relocated data ZBI!\n",
             zbi.storage().size(), arch::kZbiBootDataAlignment);
      abort();
    }
    if (auto result = zbi.Copy(relocated_zbi.data(), zbi.begin(), zbi.end()); result.is_error()) {
      zbi.ignore_error();
      printf("physboot: Failed to relocate data ZBI: ");
      zbitl::PrintViewCopyError(result.error_value());
      printf("\n");
      abort();
    }
    ZX_ASSERT(zbi.take_error().is_ok());
    boot.DataZbi().storage() = relocated_zbi.data();
  }

  // Repurpose the storage item as a place to put the handoff payload.
  // kernel_item is actually a pointer to what we already need, but it's the
  // wrong type and there's no random-access way to recover the right iterator;
  // and it's not the right pointer any more if we just relocated the data ZBI.
  auto handoff_item = boot.DataZbi().begin();
  while (handoff_item.item_offset() != kernel_item.item_offset()) {
    ZX_ASSERT(handoff_item != boot.DataZbi().end());
    ++handoff_item;
  }
  ZX_ASSERT(handoff_item->header->type == ZBI_TYPE_STORAGE_KERNEL);
  auto handoff_payload = handoff_item->payload;
  ZX_ASSERT(handoff_payload.size() >= sizeof(PhysHandoff));
  static_assert(alignof(PhysHandoff) <= ZBI_ALIGNMENT);

  // Initialize the handoff payload.  For now, it's just the boot timestamps.
  //
  // TODO(fxbug.dev/32414): There are no time samples taken in physboot after
  // the decompression is done, since it's not doing much else yet.  The first
  // sample taken by the kernel proper measures the interval containing all of
  // physboot's "handoff" work.  Additional time samples for more substantial
  // stages of pre-handoff setup work will be added as physboot starts doing
  // more work for the kernel.
  auto handoff = new (handoff_payload.data()) PhysHandoff;
  handoff->times = gBootTimes;

  boot.Boot();
}

// TODO(fxbug.dev/53593): BootOptions already parsed and redacted, so put it
// back.
void UnredactEntropyMixin(zbitl::ByteView payload) {
  constexpr ktl::string_view kPrefix = "kernel.entropy-mixin=";
  if (gBootOptions->entropy_mixin.len > 0) {
    ktl::string_view cmdline{
        reinterpret_cast<const char*>(payload.data()),
        payload.size(),
    };
    for (ktl::string_view word : WordView(cmdline)) {
      if (ktl::starts_with(word, kPrefix)) {
        word.remove_prefix(kPrefix.size());
        memcpy(const_cast<char*>(word.data()), gBootOptions->entropy_mixin.hex.data(),
               std::min(gBootOptions->entropy_mixin.len, word.size()));
        const_cast<BootOptions*>(gBootOptions)->entropy_mixin = {};
        break;
      }
    }
  }
}

[[noreturn]] void BadZbi(BootZbi::InputZbi zbi, size_t count,
                         ktl::optional<BootZbi::InputZbi::Error> error) {
  printf("physboot: Invalid ZBI of %zu bytes, %zu items: ", zbi.size_bytes(), count);

  if (error) {
    zbitl::PrintViewError(*error);
    printf("\n");
  } else {
    printf("No STORAGE_KERNEL item found!\n");
  }

  for (auto [header, payload] : zbi) {
    ktl::string_view name = zbitl::TypeName(header->type);
    if (name.empty()) {
      name = "unknown!";
    }
    printf(
        "\
physboot: Item @ %#08x size %#08x type %#08x (%.*s) extra %#08x flags %#08x\n",
        static_cast<uint32_t>(payload.data() - zbi.storage().data()), header->length, header->type,
        static_cast<int>(name.size()), name.data(), header->extra, header->flags);
  }
  zbi.ignore_error();
  abort();
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

  auto zbi_header = static_cast<const zbi_header_t*>(zbi_ptr);
  BootZbi::InputZbi zbi(zbitl::StorageFromRawHeader(zbi_header));

  BootZbi::InputZbi::iterator kernel_item = zbi.end();
  size_t count = 0;
  for (auto it = zbi.begin(); it != zbi.end(); ++it) {
    ++count;
    auto [header, payload] = *it;
    switch (header->type) {
      case ZBI_TYPE_STORAGE_KERNEL:
        kernel_item = it;
        break;
      case ZBI_TYPE_CMDLINE:
        UnredactEntropyMixin(payload);
        break;
    }
  }

  if (auto result = zbi.take_error(); result.is_error()) {
    BadZbi(zbi, count, result.error_value());
  }

  if (kernel_item == zbi.end()) {
    BadZbi(zbi, count, ktl::nullopt);
  }

  // TODO(mcgrathr): Bloat the binary so the total kernel.zbi size doesn't
  // get too comfortably small while physboot functionality is still growing.
  static const ktl::array<char, 512 * 1024> kPad{1};
  __asm__ volatile("" ::"m"(kPad), "r"(kPad.data()));

  BootZircon(zbi, kernel_item);
}
