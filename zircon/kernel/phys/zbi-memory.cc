// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/view.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <phys/main.h>

void InitMemory(void* zbi) {
  zbitl::View<zbitl::ByteView> view{
      zbitl::StorageFromRawHeader(static_cast<const zbi_header_t*>(zbi))};

  auto it = view.find(ZBI_TYPE_MEM_CONFIG);
  if (auto result = view.take_error(); result.is_error()) {
    zbitl::PrintViewError(std::move(result).error_value());
    ZX_PANIC("error occured while parsing the data ZBI");
  }
  ZX_ASSERT_MSG(it != view.end(), "no MEM_CONFIG item found in the data ZBI");
  ktl::span<zbi_mem_range_t> zbi_ranges = {
      const_cast<zbi_mem_range_t*>(reinterpret_cast<const zbi_mem_range_t*>(it->payload.data())),
      it->payload.size() / sizeof(zbi_mem_range_t),
  };

  ZbiInitMemory(zbi, zbi_ranges);
}
