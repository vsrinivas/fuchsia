// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/arm64/system.h>
#include <lib/arch/cache.h>
#include <lib/boot-options/boot-options.h>
#include <lib/memalloc/pool.h>
#include <lib/memalloc/range.h>
#include <lib/page-table/arch/arm64/builder.h>
#include <lib/page-table/builder.h>
#include <lib/page-table/types.h>
#include <zircon/boot/image.h>

#include <ktl/byte.h>
#include <ktl/optional.h>
#include <phys/allocation.h>
#include <phys/page-table.h>

#include <ktl/enforce.h>

namespace {

using page_table::AddressSpaceBuilder;
using page_table::Paddr;
using page_table::Vaddr;

// Page table layout used by physboot.
constexpr auto kDefaultPageTableLayout = page_table::arm64::PageTableLayout{
    .granule_size = page_table::arm64::GranuleSize::k4KiB,
    // Support up to 39-bits of addressable memory (2**39 == 512 GiB).
    //
    // 39 bits of memory with 4 kiB granule requires 3 levels of page table.
    .region_size_bits = 39,
};

// Set the Intermediate Physical address Size (IPS) or Physical address Size (PS)
// value of the ArmTcrElX register.
//
// This value in the register limits the range of addressable physical memory.
void SetPhysicalAddressSize(arch::ArmTcrEl1& tcr, arch::ArmPhysicalAddressSize size) {
  tcr.set_ips(size);
}
void SetPhysicalAddressSize(arch::ArmTcrEl2& tcr, arch::ArmPhysicalAddressSize size) {
  tcr.set_ps(size);
}

// Set up and enable the MMU with the given page table root.
//
// The template parameters indicate which hardware registers to use,
// and will depend at which EL level we are running at.
template <typename TcrReg, typename SctlrReg, typename Ttbr0Reg, typename MairReg>
void EnablePagingForEl(Paddr ttbr0_root) {
  // Ensure caches and MMU disabled.
  arch::ArmSystemControlRegister sctlr_reg = SctlrReg::Read();
  ZX_ASSERT(!sctlr_reg.m() && !sctlr_reg.c());

  // Clear out the data and instruction caches, and all TLBs.
  arch::InvalidateLocalCaches();
  arch::InvalidateLocalTlbs();
  __dsb(ARM_MB_SY);
  __isb(ARM_MB_SY);

  // Set up the Memory Attribute Indirection Register (MAIR).
  MairReg::Write(AddressSpaceBuilder::GetArmMemoryAttrIndirectionRegister().reg_value());

  // Configure the page table layout of TTBR0 and enable page table caching.
  TcrReg tcr;
  tcr.set_tg0(arch::ArmTcrTg0Value::k4KiB)                      // Use 4 KiB granules.
      .set_t0sz(64 - kDefaultPageTableLayout.region_size_bits)  // Set region size.
      .set_sh0(arch::ArmTcrShareAttr::kInnerShareable)
      .set_orgn0(arch::ArmTcrCacheAttr::kWriteBackWriteAllocate)
      .set_irgn0(arch::ArmTcrCacheAttr::kWriteBackWriteAllocate);

  // Allow the CPU to access all of its supported physical address space.
  // If the hardware declares it has support for 52bit PA addresses but we're only
  // using 4K page granules, downgrade to 48 bit PA range. Selecting 52 bits in the TCR
  // in this configuration is treated as reserved.
  auto pa_range = arch::ArmIdAa64Mmfr0El1::Read().pa_range();
  if (pa_range == arch::ArmPhysicalAddressSize::k52Bits) {
    pa_range = arch::ArmPhysicalAddressSize::k48Bits;
  }
  SetPhysicalAddressSize(tcr, pa_range);

  // Commit the TCR register.
  tcr.Write();
  __isb(ARM_MB_SY);

  // Set the root of the page table.
  Ttbr0Reg::Write(Ttbr0Reg{}.set_addr(ttbr0_root.value()));
  __isb(ARM_MB_SY);

  // Enable MMU and caches.
  SctlrReg::Modify([](auto& reg) {
    reg.set_m(true)    // Enable MMU
        .set_c(true)   // Allow data caches
        .set_i(true);  // Enable instruction caches.
  });
  __isb(ARM_MB_SY);
}

// Set up the MMU, having it use the given page table root.
//
// This will perform the correct operations based on the current exception level
// of the processor.
void EnablePaging(Paddr root) {
  // Set up page table for EL1 or EL2, depending on which mode we are running in.
  const auto current_el = arch::ArmCurrentEl::Read().el();
  switch (current_el) {
    case 1:
      return EnablePagingForEl<arch::ArmTcrEl1, arch::ArmSctlrEl1, arch::ArmTtbr0El1,
                               arch::ArmMairEl1>(root);
    case 2:
      return EnablePagingForEl<arch::ArmTcrEl2, arch::ArmSctlrEl2, arch::ArmTtbr0El2,
                               arch::ArmMairEl2>(root);
    default:
      ZX_PANIC("Unsupported ARM64 exception level: %u", static_cast<uint8_t>(current_el));
  }
}

void CreateBootstrapPageTable() {
  auto& pool = Allocation::GetPool();
  AllocationMemoryManager manager(pool);
  // Create a page table data structure.
  ktl::optional builder =
      page_table::arm64::AddressSpaceBuilder::Create(manager, kDefaultPageTableLayout);
  if (!builder.has_value()) {
    ZX_PANIC("Failed to create an AddressSpaceBuilder.");
  }

  bool map_device_memory = gBootOptions->phys_map_all_device_memory;

  // If we are mapping in all peripheral ranges, then the UART page will be
  // mapped below along with the rest.
  if (!map_device_memory) {
    MapUart(*builder, pool);
  }

  auto map = [map_device_memory, &builder](const memalloc::Range& range) {
    if (range.type == memalloc::Type::kReserved ||
        (range.type == memalloc::Type::kPeripheral && !map_device_memory)) {
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

  // Map in all RAM as normal memory and, depending on the value of
  // kernel.arm64.phys.map-all-device-memory, all peripheral ranges as device
  // memory.
  //
  // We merge ranges of kFreeRam or extended type on the fly, mapping the
  // previously constructed range when we have hit a hole or the end.
  constexpr auto normalize_range = [](const memalloc::Range& range) -> memalloc::Range {
    if (memalloc::IsExtendedType(range.type)) {
      auto normalized = range;
      normalized.type = memalloc::Type::kFreeRam;
      return normalized;
    }
    return range;
  };

  ktl::optional<memalloc::Range> prev;
  for (const memalloc::Range& raw_range : pool) {
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

}  // namespace

void ArchSetUpAddressSpaceEarly() {
  if (gBootOptions->phys_mmu) {
    CreateBootstrapPageTable();
  }
}

void ArchSetUpAddressSpaceLate() {}
