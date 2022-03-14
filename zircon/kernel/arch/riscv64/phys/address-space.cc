// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/cache.h>
#include <lib/memalloc/pool.h>
#include <lib/memalloc/range.h>
#include <lib/page-table/arch/riscv64/builder.h>
#include <lib/page-table/builder.h>
#include <lib/page-table/types.h>
#include <zircon/boot/image.h>

#include <ktl/byte.h>
#include <ktl/optional.h>
#include <phys/allocation.h>
#include <phys/page-table.h>

namespace {

using page_table::AddressSpaceBuilder;
using page_table::Paddr;
using page_table::Vaddr;

// Page table layout used by physboot.
constexpr auto kDefaultPageTableLayout = page_table::riscv64::PageTableLayout{
    .granule_size = page_table::riscv64::GranuleSize::k4KiB,
    // Support up to 39-bits of addressable memory (2**39 == 512 GiB).
    //
    // 39 bits of memory with 4 kiB granule requires 3 levels of page table.
    .region_size_bits = 39,
};

// Set up and enable the MMU with the given page table root.
void EnablePaging(Paddr root) {
  // Ensure the MMU is disabled.
  arch::RiscvSatp satp_reg = arch::RiscvSatp::Read();
  ZX_ASSERT(satp_reg.mode() == arch::RiscvSatpModeValue::kNone);

  // Clear out the instruction caches, and all TLBs.
  arch::InvalidateLocalTlbs();

  // Configure the page table layout and the root of the page table.
  satp_reg.Write(arch::RiscvSatp{}
                   .set_mode(arch::RiscvSatpModeValue::kSv39)
                   .set_asid(0)
                   .set_ppn(root.value() >> 12));
}

}  // namespace

void ArchSetUpAddressSpaceEarly() {
  auto& pool = Allocation::GetPool();
  AllocationMemoryManager manager(pool);
  // Create a page table data structure.
  ktl::optional builder =
      page_table::riscv64::AddressSpaceBuilder::Create(manager, kDefaultPageTableLayout);
  if (!builder.has_value()) {
    ZX_PANIC("Failed to create an AddressSpaceBuilder.");
  }

  // Maps in the given range, doing nothing if it is reserved.
  auto map = [&builder](const memalloc::MemRange& range) {
    if (range.type == memalloc::Type::kReserved) {
      return;
    }

    auto result = builder->MapRegion(Vaddr(range.addr), Paddr(range.addr), range.size,
                                     range.type == memalloc::Type::kPeripheral
                                         ? page_table::CacheAttributes::kDevice
                                         : page_table::CacheAttributes::kNormal);
    if (result != ZX_OK) {
      ZX_PANIC("Failed to map in range.");
    }
  };
  // Map in all memory regions.
  //
  // We merge ranges of kFreeRam or extended type on the fly, mapping the
  // previously constructed range when we have hit a hole or the end.
  constexpr auto normalize_range = [](const memalloc::MemRange& range) -> memalloc::MemRange {
    if (memalloc::IsExtendedType(range.type)) {
      auto normalized = range;
      normalized.type = memalloc::Type::kFreeRam;
      return normalized;
    }
    return range;
  };

  std::optional<memalloc::MemRange> prev;
  for (const memalloc::MemRange& raw_range : pool) {
    auto range = normalize_range(raw_range);
    if (!prev) {
      prev = range;
    } else if (prev->end() == range.addr && prev->type == range.type) {
      prev->size += range.size;
    } else {
      map(*prev);
      prev = range;
    }
  }

  ZX_DEBUG_ASSERT(prev);
  map(*prev);

  // Enable the MMU and switch to the new page table.
  EnablePaging(builder->root_paddr());
}

void ArchSetUpAddressSpaceLate() {}
