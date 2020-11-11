// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015-2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ASPACE_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ASPACE_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <arch/arm64/mmu.h>
#include <fbl/canary.h>
#include <kernel/mutex.h>
#include <vm/arch_vm_aspace.h>

class ArmArchVmAspace final : public ArchVmAspaceInterface {
 public:
  ArmArchVmAspace();
  virtual ~ArmArchVmAspace();

  using ArchVmAspaceInterface::page_alloc_fn_t;

  zx_status_t Init(vaddr_t base, size_t size, uint mmu_flags,
                   page_alloc_fn_t test_paf = nullptr) override;

  zx_status_t Destroy() override;

  // main methods
  zx_status_t Map(vaddr_t vaddr, paddr_t* phys, size_t count, uint mmu_flags,
                  size_t* mapped) override;
  zx_status_t MapContiguous(vaddr_t vaddr, paddr_t paddr, size_t count, uint mmu_flags,
                            size_t* mapped) override;

  zx_status_t Unmap(vaddr_t vaddr, size_t count, size_t* unmapped) override;

  zx_status_t Protect(vaddr_t vaddr, size_t count, uint mmu_flags) override;

  zx_status_t Query(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) override;

  vaddr_t PickSpot(vaddr_t base, uint prev_region_mmu_flags, vaddr_t end,
                   uint next_region_mmu_flags, vaddr_t align, size_t size, uint mmu_flags) override;

  zx_status_t MarkAccessed(vaddr_t vaddr, size_t count) override;

  zx_status_t HarvestAccessed(vaddr_t vaddr, size_t count,
                              const HarvestCallback& accessed_callback) override;

  paddr_t arch_table_phys() const override { return tt_phys_; }
  uint16_t arch_asid() const { return asid_; }
  void arch_set_asid(uint16_t asid) { asid_ = asid; }

  static void ContextSwitch(ArmArchVmAspace* from, ArmArchVmAspace* to);

 private:
  inline bool IsValidVaddr(vaddr_t vaddr) { return (vaddr >= base_ && vaddr <= base_ + size_ - 1); }

  zx_status_t AllocPageTable(paddr_t* paddrp, uint page_size_shift) TA_REQ(lock_);

  void FreePageTable(void* vaddr, paddr_t paddr, uint page_size_shift) TA_REQ(lock_);

  ssize_t MapPageTable(vaddr_t vaddr_in, vaddr_t vaddr_rel_in, paddr_t paddr_in, size_t size_in,
                       pte_t attrs, uint index_shift, uint page_size_shift,
                       volatile pte_t* page_table) TA_REQ(lock_);

  ssize_t UnmapPageTable(vaddr_t vaddr, vaddr_t vaddr_rel, size_t size, uint index_shift,
                         uint page_size_shift, volatile pte_t* page_table) TA_REQ(lock_);

  zx_status_t ProtectPageTable(vaddr_t vaddr_in, vaddr_t vaddr_rel_in, size_t size_in, pte_t attrs,
                               uint index_shift, uint page_size_shift, volatile pte_t* page_table)
      TA_REQ(lock_);

  bool HarvestAccessedPageTable(vaddr_t vaddr_in, vaddr_t vaddr_rel_in, size_t size_in,
                                uint index_shift, uint page_size_shift, volatile pte_t* page_table,
                                const HarvestCallback& accessed_callback) TA_REQ(lock_);

  void MarkAccessedPageTable(vaddr_t vaddr, vaddr_t vaddr_rel_in, size_t size, uint index_shift,
                             uint page_size_shift, volatile pte_t* page_table) TA_REQ(lock_);

  // Splits a descriptor block into a set of next-level-down page blocks/pages.
  //
  // |vaddr| is the virtual address of the start of the block being split. |index_shift| is
  // the index shift of the page table entry of the descriptor blocking being split.
  // |page_size_shift| is the page size shift of the current aspace. |page_table| is the
  // page table that contains the descriptor block being split, and |pt_index| is the index
  // into that table.
  zx_status_t SplitLargePage(vaddr_t vaddr, uint index_shift, uint page_size_shift,
                             vaddr_t pt_index, volatile pte_t* page_table) TA_REQ(lock_);

  void MmuParamsFromFlags(uint mmu_flags, pte_t* attrs, vaddr_t* vaddr_base, uint* top_size_shift,
                          uint* top_index_shift, uint* page_size_shift);
  ssize_t MapPages(vaddr_t vaddr, paddr_t paddr, size_t size, pte_t attrs, vaddr_t vaddr_base,
                   uint top_size_shift, uint top_index_shift, uint page_size_shift) TA_REQ(lock_);

  ssize_t UnmapPages(vaddr_t vaddr, size_t size, vaddr_t vaddr_base, uint top_size_shift,
                     uint top_index_shift, uint page_size_shift) TA_REQ(lock_);

  zx_status_t ProtectPages(vaddr_t vaddr, size_t size, pte_t attrs, vaddr_t vaddr_base,
                           uint top_size_shift, uint top_index_shift, uint page_size_shift)
      TA_REQ(lock_);
  zx_status_t QueryLocked(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) TA_REQ(lock_);

  void FlushTLBEntry(vaddr_t vaddr, bool terminal) TA_REQ(lock_);

  uint MmuFlagsFromPte(pte_t pte);

  // data fields
  fbl::Canary<fbl::magic("VAAS")> canary_;

  DECLARE_MUTEX(ArmArchVmAspace) lock_;

  // Page allocate function, if set will be used instead of the default allocator
  page_alloc_fn_t test_page_alloc_func_ = nullptr;

  uint16_t asid_ = MMU_ARM64_UNUSED_ASID;

  // Pointer to the translation table.
  paddr_t tt_phys_ = 0;
  volatile pte_t* tt_virt_ = nullptr;

  // Upper bound of the number of pages allocated to back the translation
  // table.
  size_t pt_pages_ = 0;

  uint flags_ = 0;

  // Range of address space.
  vaddr_t base_ = 0;
  size_t size_ = 0;
};

static inline paddr_t arm64_vttbr(uint16_t vmid, paddr_t baddr) {
  return static_cast<paddr_t>(vmid) << 48 | baddr;
}

using ArchVmAspace = ArmArchVmAspace;

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ASPACE_H_
