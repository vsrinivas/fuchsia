// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/boot-options/boot-options.h>
#include <lib/memalloc/pool.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <ktl/string_view.h>
#include <phys/allocation.h>
#include <phys/main.h>
#include <phys/page-table.h>
#include <phys/symbolize.h>

using ZbiView = zbitl::View<zbitl::ByteView>;

void InitMemory(void* zbi) {
  zbitl::View<zbitl::ByteView> view{
      zbitl::StorageFromRawHeader(static_cast<const zbi_header_t*>(zbi))};

  auto it = view.begin();
  while (it != view.end() && it->header->type != ZBI_TYPE_MEM_CONFIG) {
    ++it;
  }
  if (auto result = view.take_error(); result.is_error()) {
    zbitl::PrintViewError(std::move(result).error_value());
    ZX_PANIC("error occured while parsing the data ZBI");
  }
  ZX_ASSERT_MSG(it != view.end(), "no MEM_CONFIG item found in the data ZBI");
  ktl::span<zbi_mem_range_t> zbi_ranges = {
      const_cast<zbi_mem_range_t*>(reinterpret_cast<const zbi_mem_range_t*>(it->payload.data())),
      it->payload.size() / sizeof(zbi_mem_range_t),
  };

  uint64_t phys_start = reinterpret_cast<uint64_t>(PHYS_LOAD_ADDRESS);
  uint64_t phys_end = reinterpret_cast<uint64_t>(_end);
  memalloc::MemRange ranges[] = {
      {
          .addr = phys_start,
          .size = phys_end - phys_start,
          .type = memalloc::Type::kPhysKernel,
      },
      {
          .addr = reinterpret_cast<uint64_t>(zbi),
          .size = static_cast<uint64_t>(view.size_bytes()),
          .type = memalloc::Type::kDataZbi,
      },
  };

  std::array all_ranges = {
      memalloc::AsMemRanges(zbi_ranges),
      ktl::span<memalloc::MemRange>{ranges},
  };

  auto& pool = Allocation::GetPool();
  ZX_ASSERT(pool.Init(all_ranges).is_ok());

  // Set up our own address space.
  ArchSetUpAddressSpaceEarly();

  if (gBootOptions->phys_verbose) {
    pool.PrintMemoryRanges(Symbolize::kProgramName_);
  }
}
