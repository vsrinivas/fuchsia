// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "phys/kernel-package.h"

#include <lib/boot-options/boot-options.h>
#include <lib/boot-options/word-view.h>
#include <lib/memalloc/range.h>
#include <lib/zbitl/error-stdio.h>
#include <lib/zbitl/view.h>
#include <string.h>

#include <ktl/algorithm.h>
#include <ktl/move.h>
#include <ktl/optional.h>
#include <ktl/string_view.h>
#include <phys/stdio.h>
#include <phys/symbolize.h>
#include <phys/zbitl-allocation.h>
#include <pretty/cpp/sizes.h>

#include <ktl/enforce.h>

namespace {

[[noreturn]] void BadZbi(KernelStorage::Zbi zbi, size_t count,
                         ktl::optional<KernelStorage::Zbi::Error> error) {
  printf("%s: Invalid ZBI of %zu bytes, %zu items: ", ProgramName(), zbi.size_bytes(), count);

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
%s: Item @ %#08x size %#08x type %#08x (%.*s) extra %#08x flags %#08x\n",
        ProgramName(), static_cast<uint32_t>(payload.data() - zbi.storage().data()), header->length,
        header->type, static_cast<int>(name.size()), name.data(), header->extra, header->flags);
  }
  zbi.ignore_error();
  abort();
}

}  // namespace

void KernelStorage::Init(Zbi zbi) {
  zbi_ = ktl::move(zbi);
  item_ = zbi_.end();

  size_t count = 0;
  for (auto it = zbi_.begin(); it != zbi_.end(); ++it) {
    ++count;
    if (it->header->type == ZBI_TYPE_STORAGE_KERNEL) {
      item_ = it;
      break;
    }
  }

  if (auto result = zbi_.take_error(); result.is_error()) {
    BadZbi(zbi_, count, result.error_value());
  }

  if (item_ == zbi_.end()) {
    BadZbi(zbi_, count, ktl::nullopt);
  }

  fbl::AllocChecker ac;
  const uint32_t storage_size = zbitl::UncompressedLength(*item_->header);
  storage_ =
      Allocation::New(ac, memalloc::Type::kKernelStorage, storage_size, ZBI_BOOTFS_PAGE_SIZE);
  if (!ac.check()) {
    printf("%s: Cannot allocate %#x bytes for decompressed STORAGE_KERNEL item!\n", ProgramName(),
           storage_size);
    abort();
  }

  // This marks the interval from completing basic phys environment setup
  // (kPhysSetup) to when the ZBI has been decoded enough to start accessing
  // the real kernel payload (which is usually compressed).
  decompress_start_ts_ = arch::EarlyTicks::Get();

  if (auto result = zbi_.CopyStorageItem(data(), item_, ZbitlScratchAllocator); result.is_error()) {
    printf("%s: Cannot load STORAGE_KERNEL item (uncompressed size %#x): ", ProgramName(),
           storage_size);
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  // This marks just the decompression (or copying) time.
  decompress_end_ts_ = arch::EarlyTicks::Get();

  debugf("%s: STORAGE_KERNEL decompressed %s -> %s\n", ProgramName(),
         pretty::FormattedBytes(item_->header->length).c_str(),
         pretty::FormattedBytes(storage_size).c_str());

  if (auto result = BootfsReader::Create(data()); result.is_error()) {
    printf("%s: cannot open BOOTFS image from KERNEL_STORAGE item (%#zx bytes at %p): ",
           ProgramName(), data().size(), data().data());
    zbitl::PrintBootfsError(result.error_value());
    abort();
  } else {
    bootfs_reader_ = ktl::move(result).value();
  }
}
