// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/arch/zbi-boot.h>
#include <lib/boot-options/boot-options.h>
#include <lib/boot-options/word-view.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <ktl/optional.h>
#include <pretty/sizes.h>

#include "allocation.h"
#include "boot-zbi.h"
#include "main.h"
#include "symbolize.h"
#include "zbitl-allocation.h"

const char Symbolize::kProgramName_[] = "physboot";

namespace {

// The boot_alloc code uses arbitrary pages after the official bss space.
// So make sure to allocate some extra slop for the kernel.
constexpr uint64_t kKernelBootAllocReserve = 1024 * 1024 * 4;

// A guess about the upper bound on reserve_memory_size so we can do a single
// allocation before decoding the header and probably not need to relocate.
constexpr uint64_t kKernelBssEstimate = 1024 * 1024 * 2;

struct LoadedZircon {
  Allocation buffer;
  BootZbi boot;
  arch::EarlyTicks decompress_ts;
};

LoadedZircon LoadZircon(BootZbi::InputZbi& zbi, BootZbi::InputZbi::iterator kernel_item,
                        uint64_t reserve_memory_estimate) {
  fbl::AllocChecker ac;
  const auto kernel_length = zbitl::UncompressedLength(*(*kernel_item).header);
  auto buffer_sizes = BootZbi::SuggestedAllocation(kernel_length);

  // That covers the uncompressed size of the image alone.  Preallocate enough
  // space after it that the bss and boot_alloc reserve are likely to fit.
  buffer_sizes.size += reserve_memory_estimate + kKernelBootAllocReserve;

  auto buffer = Allocation::New(ac, buffer_sizes.size, buffer_sizes.alignment);
  if (!ac.check()) {
    printf(
        "physboot: Cannot allocate %#zx bytes aligned to %#zx for decompressed kernel payload!\n",
        buffer_sizes.size, buffer_sizes.alignment);
    abort();
  }

  if (auto result = zbi.CopyStorageItem(buffer.data(), kernel_item, ZbitlScratchAllocator);
      result.is_error()) {
    printf("physboot: Cannot load STORAGE_KERNEL item (uncompressed size %#x): ", kernel_length);
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  auto decompress_ts = arch::EarlyTicks::Get();

  BootZbi::InputZbi kernel_zbi(zbitl::AsBytes(buffer.data()));

  {
    char buf1[MAX_FORMAT_SIZE_LEN], buf2[MAX_FORMAT_SIZE_LEN];
    printf("physboot: STORAGE_KERNEL decompressed %s -> %s\n",
           format_size(buf1, sizeof(buf1), (*kernel_item).header->length),
           format_size(buf2, sizeof(buf2), kernel_zbi.size_bytes()));
  }

  BootZbi boot;
  if (auto result = boot.Init(kernel_zbi); result.is_error()) {
    printf("physboot: Cannot read STORAGE_KERNEL item ZBI: ");
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  return {std::move(buffer), std::move(boot), decompress_ts};
}

[[noreturn]] void BootZircon(BootZbi::InputZbi& zbi, BootZbi::InputZbi::iterator kernel_item,
                             arch::EarlyTicks entry_ts) {
  auto zircon = LoadZircon(zbi, kernel_item, kKernelBssEstimate);
  if (!zircon.boot.KernelCanLoadInPlace() ||
      zircon.buffer.size_bytes() < zircon.boot.KernelMemorySize() + kKernelBootAllocReserve) {
    printf("physboot: Kernel ZBI at %#" PRIx64 " cannot be loaded in place!\n",
           zircon.boot.KernelLoadAddress());
    uint64_t bss_size = zircon.boot.KernelHeader()->reserve_memory_size;
    printf("physboot: BSS size %#" PRIx64 " > estimate %#" PRIx64 "\n", bss_size,
           kKernelBssEstimate);
    ZX_ASSERT(bss_size > kKernelBssEstimate);
    zircon = {};  // Free old resources.
    printf("physboot: Repeating decompression with larger buffer...\n");
    zircon = LoadZircon(zbi, kernel_item, bss_size);
  }
  auto& [buffer, boot, decompress_ts] = zircon;

  ZX_ASSERT(boot.KernelCanLoadInPlace());
  ZX_ASSERT_MSG(buffer.size_bytes() >= boot.KernelMemorySize() + kKernelBootAllocReserve,
                "Kernel allocation %#zx too small for load size %#x +"
                " bss %#" PRIx64 " + boot_alloc reserve %#" PRIx64 "\n",
                buffer.size_bytes(), boot.KernelLoadSize(),
                boot.KernelHeader()->reserve_memory_size, kKernelBootAllocReserve);

  // TODO(mcgrathr): propagate timestamps

  // The kernel is now in place, but it will just use the original data ZBI.
  boot.DataZbi().storage() = {
      const_cast<std::byte*>(zbi.storage().data()),
      zbi.storage().size(),
  };

  char buf[MAX_FORMAT_SIZE_LEN];
  printf("physboot: Kernel @ [0x%016" PRIxPTR ", 0x%016" PRIxPTR ")  %s\n",
         boot.KernelLoadAddress(), boot.KernelLoadAddress() + boot.KernelLoadSize(),
         format_size(buf, sizeof(buf), boot.KernelLoadSize()));
  printf("physboot:  Entry @  0x%016" PRIxPTR "\n", boot.KernelEntryAddress());
  printf("physboot:    BSS @ [0x%016" PRIxPTR ", 0x%016" PRIxPTR ")  %s\n",
         boot.KernelLoadAddress() + boot.KernelLoadSize(),
         boot.KernelLoadAddress() + boot.KernelMemorySize(),
         format_size(buf, sizeof(buf), boot.KernelHeader()->reserve_memory_size));
  printf("physboot: ZBI    @ [0x%016" PRIxPTR ", 0x%016" PRIxPTR ")  %s\n", boot.DataLoadAddress(),
         boot.DataLoadAddress() + boot.DataLoadSize(),
         format_size(buf, sizeof(buf), boot.DataLoadSize()));

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
    for (auto word : WordView(cmdline)) {
      if (word.starts_with(kPrefix)) {
        word.remove_prefix(kPrefix.size());
        memcpy(const_cast<char*>(word.data()), gBootOptions->entropy_mixin.hex.data(),
               std::min(gBootOptions->entropy_mixin.len, word.size()));
        gBootOptions->entropy_mixin = {};
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
    auto name = zbitl::TypeName(header->type);
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
  Symbolize::GetInstance()->Context();

  InitMemory(zbi_ptr);

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

  BootZircon(zbi, kernel_item, ticks);
}
