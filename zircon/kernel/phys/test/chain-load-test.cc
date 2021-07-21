// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/fitx/result.h>
#include <lib/memalloc/range.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>

#include <ktl/byte.h>
#include <ktl/string_view.h>
#include <phys/allocation.h>
#include <phys/boot-zbi.h>
#include <phys/zbitl-allocation.h>

#include "test-main.h"

const char Symbolize::kProgramName_[] = "chain-load-test";

constexpr uint32_t kLoadType = ZBI_TYPE_STORAGE_RAMDISK;

namespace {

Allocation Allocate(BootZbi::Size need, const char* for_what) {
  fbl::AllocChecker ac;
  auto result = Allocation::New(ac, memalloc::Type::kZbiTestPayload, need.size, need.alignment);
  if (!ac.check()) {
    printf("Cannot allocate %s size=%#zx alignment=%#zx\n", for_what, need.size, need.alignment);
  }
  return result;
}

}  // namespace

int TestMain(void* zbi_ptr, arch::EarlyTicks ticks) {
  InitMemory(zbi_ptr);

  auto zbi_header = static_cast<const zbi_header_t*>(zbi_ptr);
  auto zbi_bytes = zbitl::StorageFromRawHeader(zbi_header);
  zbitl::View<zbitl::ByteView> zbi(zbi_bytes);

  // Find the first RAMDISK item.  We'll ignore anything before that, though
  // there probably isn't anything except maybe an embedded cmdline.  Anything
  // later is included as "boot loader" items for the next ZBI kernel.
  auto rest = zbi.end();
  auto loadit = zbi.end();
  size_t items = 0;
  for (auto it = zbi.begin(); it != zbi.end(); ++it) {
    ++items;
    if (it->header->type == kLoadType) {
      loadit = it;
      rest = ++it;
      break;
    }
  }

  const auto rest_size_bytes = zbi.size_bytes() - rest.item_offset();

  if (auto result = zbi.take_error(); result.is_error()) {
    printf("ZBI error finding RAMDISK: ");
    zbitl::PrintViewError(result.error_value());
    return 1;
  }

  if (loadit == zbi.end()) {
    printf("ZBI of %zu bytes has no RAMDISK in %zu items\n", zbi.size_bytes(), items);
    return 1;
  }

  const auto length = zbitl::UncompressedLength(*loadit->header);
  auto load_buffer_size = BootZbi::SuggestedAllocation(length);
  load_buffer_size.size += rest_size_bytes;
  auto load_buffer = Allocate(load_buffer_size, "payload");
  if (!load_buffer) {
    return 1;
  }

  if (auto result = zbi.CopyStorageItem(load_buffer.data(), loadit, ZbitlScratchAllocator);
      result.is_error()) {
    zbitl::PrintViewCopyError(result.error_value());
    return 1;
  }

  BootZbi::InputZbi load_zbi(zbitl::AsBytes(load_buffer.data()));
  printf("ZBI payload item of %u bytes decompressed into %zu of %u bytes\n", loadit->header->length,
         load_zbi.size_bytes(), length);

  BootZbi boot;
  if (auto result = boot.Init(load_zbi); result.is_error()) {
    stdout->Write("Payload ZBI not bootable: ");
    zbitl::PrintViewCopyError(result.error_value());
    return 1;
  }

  if (auto result = boot.Load(rest_size_bytes); result.is_error()) {
    stdout->Write("Failed to load payload ZBI: ");
    zbitl::PrintViewCopyError(result.error_value());
    return 1;
  }

  printf("Loaded kernel and data; data ZBI occupies %#zx of %#zx bytes.\n",
         boot.DataZbi().size_bytes(), boot.DataZbi().storage().size());

  if (auto result = boot.DataZbi().Extend(rest, zbi.end()); result.is_error()) {
    stdout->Write("Cannot append boot loader ZBI items: ");
    zbitl::PrintViewCopyError(result.error_value());
    return 1;
  }

  boot.Boot();

  printf("BootZbi::Boot() returned!\n");
  return 1;
}
