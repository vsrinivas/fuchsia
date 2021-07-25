// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <lib/memalloc/pool.h>
#include <lib/zbitl/view.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <ktl/array.h>
#include <phys/allocation.h>
#include <phys/main.h>
#include <phys/page-table.h>
#include <phys/symbolize.h>

void ZbiInitMemory(void* zbi, ktl::span<zbi_mem_range_t> mem_config) {
  zbitl::ByteView zbi_storage = zbitl::StorageFromRawHeader(static_cast<zbi_header_t*>(zbi));

  uint64_t phys_start = reinterpret_cast<uint64_t>(PHYS_LOAD_ADDRESS);
  uint64_t phys_end = reinterpret_cast<uint64_t>(_end);
  memalloc::MemRange special_memory_ranges[] = {
      {
          .addr = phys_start,
          .size = phys_end - phys_start,
          .type = memalloc::Type::kPhysKernel,
      },
      {
          .addr = reinterpret_cast<uint64_t>(zbi_storage.data()),
          .size = zbi_storage.size_bytes(),
          .type = memalloc::Type::kDataZbi,
      },
  };

  ktl::span<memalloc::MemRange> zbi_ranges(memalloc::AsMemRanges(mem_config));
  ktl::span<memalloc::MemRange> special_ranges(special_memory_ranges);

  auto& pool = Allocation::GetPool();

  auto init_result = pool.Init(ktl::array{zbi_ranges, special_ranges});
  ZX_ASSERT(init_result.is_ok());

  // Set up our own address space.
  ArchSetUpAddressSpaceEarly();

  if (gBootOptions->phys_verbose) {
    pool.PrintMemoryRanges(Symbolize::kProgramName_);
  }
}
