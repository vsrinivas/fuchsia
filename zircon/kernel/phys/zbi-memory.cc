// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/zbitl/error-stdio.h>
#include <lib/zbitl/view.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <ktl/optional.h>
#include <ktl/span.h>
#include <phys/main.h>

void InitMemory(void* zbi) {
  ktl::span<zbi_mem_range_t> zbi_ranges;
  ktl::optional<memalloc::Range> nvram_range;

  zbitl::View<zbitl::ByteView> view{
      zbitl::StorageFromRawHeader(static_cast<const zbi_header_t*>(zbi))};
  for (auto [header, payload] : view) {
    switch (header->type) {
      case ZBI_TYPE_MEM_CONFIG:
        zbi_ranges = {
            const_cast<zbi_mem_range_t*>(reinterpret_cast<const zbi_mem_range_t*>(payload.data())),
            payload.size_bytes() / sizeof(zbi_mem_range_t)};
        break;

      case ZBI_TYPE_NVRAM:
        ZX_ASSERT(payload.size_bytes() >= sizeof(zbi_nvram_t));
        const zbi_nvram_t* nvram = reinterpret_cast<const zbi_nvram_t*>(payload.data());
        nvram_range = {
            .addr = nvram->base,
            .size = nvram->length,
            .type = memalloc::Type::kNvram,
        };
        break;
    }
  }
  if (auto result = view.take_error(); result.is_error()) {
    zbitl::PrintViewError(result.error_value());
    ZX_PANIC("error occured while parsing the data ZBI");
  }

  ZX_ASSERT_MSG(!zbi_ranges.empty(), "no MEM_CONFIG item found in the data ZBI");

  ZbiInitMemory(zbi, zbi_ranges, nvram_range);
}
