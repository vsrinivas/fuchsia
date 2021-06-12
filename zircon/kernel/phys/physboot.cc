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

#include <ktl/array.h>
#include <ktl/optional.h>
#include <phys/allocation.h>
#include <phys/boot-zbi.h>
#include <phys/main.h>
#include <phys/symbolize.h>
#include <phys/zbitl-allocation.h>
#include <pretty/cpp/sizes.h>

const char Symbolize::kProgramName_[] = "physboot";

namespace {

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
  buffer_sizes.size += reserve_memory_estimate + BootZbi::kKernelBootAllocReserve;

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
    printf("physboot: STORAGE_KERNEL decompressed %s -> %s\n",
           pretty::FormattedBytes((*kernel_item).header->length).c_str(),
           pretty::FormattedBytes(kernel_zbi.size_bytes()).c_str());
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
      zircon.buffer.size_bytes() < zircon.boot.KernelMemorySize()) {
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
  ZX_ASSERT_MSG(buffer.size_bytes() >= boot.KernelMemorySize(),
                "Kernel allocation %#zx too small for load size %#x +"
                " bss %#" PRIx64 " + boot_alloc reserve %#" PRIx64 "\n",
                buffer.size_bytes(), boot.KernelLoadSize(),
                boot.KernelHeader()->reserve_memory_size, BootZbi::kKernelBootAllocReserve);

  // TODO(mcgrathr): propagate timestamps

  // The kernel is now in place, but it will just use the original data ZBI.
  boot.DataZbi().storage() = {
      const_cast<std::byte*>(zbi.storage().data()),
      zbi.storage().size(),
  };

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
      if (ktl::starts_with(word, kPrefix)) {
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

  // TODO(mcgrathr): Bloat the binary so the total kernel.zbi size doesn't
  // get too comfortably small while physboot functionality is still growing.
  static const ktl::array<char, 512 * 1024> kPad{1};
  __asm__ volatile("" ::"m"(kPad), "r"(kPad.data()));

  BootZircon(zbi, kernel_item, ticks);
}
