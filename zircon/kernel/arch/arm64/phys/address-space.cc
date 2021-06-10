// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/arm64/system.h>
#include <lib/arch/cache.h>
#include <lib/page-table/arch/arm64/builder.h>
#include <lib/page-table/builder.h>
#include <lib/page-table/types.h>

#include <ktl/byte.h>
#include <ktl/optional.h>
#include <phys/arch.h>

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
  arch::Arm64LocalInvalidateAllCaches();
  arch::InvalidateTlbs();
  __dsb(ARM_MB_SY);
  __isb(ARM_MB_SY);

  // Set up the Memory Attribute Indirection Register (MAIR).
  MairReg::Write(AddressSpaceBuilder::GetArmMemoryAttrIndirectionRegister().reg_value());

  // Configure the page table layout of TTBR0 and enable page table caching.
  TcrReg tcr = TcrReg{}
        .set_tg0(arch::ArmTcrTg0Value::k4KiB)                     // Use 4 KiB granules.
        .set_t0sz(64 - kDefaultPageTableLayout.region_size_bits)  // Set region size.
        .set_sh0(arch::ArmTcrShareAttr::kInnerShareable)
        .set_orgn0(arch::ArmTcrCacheAttr::kWriteBackWriteAllocate)
        .set_irgn0(arch::ArmTcrCacheAttr::kWriteBackWriteAllocate);

  // Allow the CPU to access all of its supported physical address space.
  SetPhysicalAddressSize(tcr, arch::ArmIdAa64Mmfr0El1::Read().pa_range());

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
  const uint8_t current_el = arch::ArmCurrentEl::Read().el();
  switch (current_el) {
    case 1:
      return EnablePagingForEl<arch::ArmTcrEl1, arch::ArmSctlrEl1, arch::ArmTtbr0El1,
                               arch::ArmMairEl1>(root);
    case 2:
      return EnablePagingForEl<arch::ArmTcrEl2, arch::ArmSctlrEl2, arch::ArmTtbr0El2,
                               arch::ArmMairEl2>(root);
    default:
      ZX_PANIC("Unsupported ARM64 exception level: %d\n", current_el);
  }
}

// A page_table::MemoryManager that uses the given allocator, and
// assumes a 1:1 mapping from physical addresses to host virtual
// addresses.
class BootstrapMemoryManager final : public page_table::MemoryManager {
 public:
  explicit BootstrapMemoryManager(memalloc::Allocator& allocator) : allocator_(&allocator) {}

  ktl::byte* Allocate(size_t size, size_t alignment) final {
    zx::status<uint64_t> result = allocator_->Allocate(size, alignment);
    if (result.is_error()) {
      return nullptr;
    }
    return reinterpret_cast<ktl::byte*>(result.value());
  }

  Paddr PtrToPhys(ktl::byte* ptr) final {
    // We have a 1:1 virtual/physical mapping.
    return Paddr(reinterpret_cast<uint64_t>(ptr));
  }

  ktl::byte* PhysToPtr(Paddr phys) final {
    // We have a 1:1 virtual/physical mapping.
    return reinterpret_cast<ktl::byte*>(phys.value());
  }

 private:
  memalloc::Allocator* allocator_;
};

void CreateBootstapPageTable(page_table::MemoryManager& allocator,
                             const zbitl::MemRangeTable& memory_map) {
  // Create a page table data structure.
  ktl::optional builder =
      page_table::arm64::AddressSpaceBuilder::Create(allocator, kDefaultPageTableLayout);
  if (!builder.has_value()) {
    ZX_PANIC("Failed to create an AddressSpaceBuilder.");
  }

  // Map in all memory regions.
  for (const auto& range : memory_map) {
    // Skip over ZBI_MEM_RANGE_RESERVED regions.
    //
    // ZBI_MEM_RANGE_RESERVED regions are allowed to overlap ranges seen in
    // previous or future memory map entries. We don't attempt to unmap such
    // overlapping regions.
    //
    // TODO(fxbug.dev/77789): Avoid mapping in reserved ranges that overlap
    // other types of RAM.
    if (range.type == ZBI_MEM_RANGE_RESERVED) {
      continue;
    }
    zx_status_t result =
        builder->MapRegion(Vaddr(range.paddr), Paddr(range.paddr), range.length,
                           range.type == ZBI_MEM_RANGE_RAM ? page_table::CacheAttributes::kNormal
                                                           : page_table::CacheAttributes::kDevice);
    if (result != ZX_OK) {
      ZX_PANIC("Failed to map in range.");
    }
  }

  // Enable the MMU and switch to the new page table.
  EnablePaging(builder->root_paddr());
}

}  // namespace

void ArchSetUpAddressSpace(memalloc::Allocator& allocator, const zbitl::MemRangeTable& table) {
  BootstrapMemoryManager manager(allocator);
  CreateBootstapPageTable(manager, table);
}
