// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_ASPACE_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_ASPACE_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <arch/x86/ioport.h>
#include <arch/x86/mmu.h>
#include <arch/x86/page_tables/page_tables.h>
#include <fbl/algorithm.h>
#include <fbl/canary.h>
#include <ktl/atomic.h>
#include <vm/arch_vm_aspace.h>
#include <vm/pmm.h>

// Implementation of page tables used by x86-64 CPUs.
class X86PageTableMmu final : public X86PageTableBase {
 public:
  using X86PageTableBase::Destroy;
  using X86PageTableBase::Init;

  // Initialize the kernel page table, assigning the given context to it.
  // This X86PageTable will be special in that its mappings will all have
  // the G (global) bit set, and are expected to be aliased across all page
  // tables used in the normal MMU.  See |AliasKernelMappings|.
  zx_status_t InitKernel(void* ctx, ArchVmAspaceInterface::page_alloc_fn_t test_paf = nullptr);

  // Used for normal MMU page tables so they can share the high kernel mapping
  zx_status_t AliasKernelMappings();

 private:
  using X86PageTableBase::ctx_;
  using X86PageTableBase::pages_;
  using X86PageTableBase::phys_;
  using X86PageTableBase::virt_;

  PageTableLevel top_level() final { return PML4_L; }
  bool allowed_flags(uint flags) final { return (flags & ARCH_MMU_FLAG_PERM_READ); }
  bool check_paddr(paddr_t paddr) final;
  bool check_vaddr(vaddr_t vaddr) final;
  bool supports_page_size(PageTableLevel level) final;
  IntermediatePtFlags intermediate_flags() final;
  PtFlags terminal_flags(PageTableLevel level, uint flags) final;
  PtFlags split_flags(PageTableLevel level, PtFlags flags) final;
  void TlbInvalidate(PendingTlbInvalidation* pending) final;
  uint pt_flags_to_mmu_flags(PtFlags flags, PageTableLevel level) final;
  bool needs_cache_flushes() final { return false; }

  // If true, all mappings will have the global bit set.
  bool use_global_mappings_ = false;
};

// Implementation of Intel's Extended Page Tables, for use in virtualization.
class X86PageTableEpt final : public X86PageTableBase {
 public:
  using X86PageTableBase::Destroy;
  using X86PageTableBase::Init;

 private:
  PageTableLevel top_level() final { return PML4_L; }
  bool allowed_flags(uint flags) final;
  bool check_paddr(paddr_t paddr) final;
  bool check_vaddr(vaddr_t vaddr) final;
  bool supports_page_size(PageTableLevel level) final;
  IntermediatePtFlags intermediate_flags() final;
  PtFlags terminal_flags(PageTableLevel level, uint flags) final;
  PtFlags split_flags(PageTableLevel level, PtFlags flags) final;
  void TlbInvalidate(PendingTlbInvalidation* pending) final;
  uint pt_flags_to_mmu_flags(PtFlags flags, PageTableLevel level) final;
  bool needs_cache_flushes() final { return false; }
};

class X86ArchVmAspace final : public ArchVmAspaceInterface {
 public:
  X86ArchVmAspace(vaddr_t base, size_t size, uint mmu_flags, page_alloc_fn_t test_paf = nullptr);
  virtual ~X86ArchVmAspace();

  using ArchVmAspaceInterface::page_alloc_fn_t;

  zx_status_t Init() override;

  zx_status_t Destroy() override;

  // main methods
  zx_status_t MapContiguous(vaddr_t vaddr, paddr_t paddr, size_t count, uint mmu_flags,
                            size_t* mapped) override;
  zx_status_t Map(vaddr_t vaddr, paddr_t* phys, size_t count, uint mmu_flags,
                  size_t* mapped) override;
  zx_status_t Unmap(vaddr_t vaddr, size_t count, size_t* unmapped) override;
  zx_status_t Protect(vaddr_t vaddr, size_t count, uint mmu_flags) override;
  zx_status_t Query(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) override;

  vaddr_t PickSpot(vaddr_t base, uint prev_region_mmu_flags, vaddr_t end,
                   uint next_region_mmu_flags, vaddr_t align, size_t size, uint mmu_flags) override;

  // On x86 the hardware can always set the accessed bit so we do not need to support the software
  // fault method.
  zx_status_t MarkAccessed(vaddr_t vaddr, size_t count) override { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t HarvestAccessed(vaddr_t vaddr, size_t count,
                              const HarvestCallback& accessed_callback) override;

  zx_status_t FreeUnaccessed(vaddr_t vaddr, size_t count) override { return ZX_ERR_NOT_SUPPORTED; }

  paddr_t arch_table_phys() const override { return pt_->phys(); }
  paddr_t pt_phys() const { return pt_->phys(); }
  size_t pt_pages() const { return pt_->pages(); }

  int active_cpus() { return active_cpus_.load(); }

  IoBitmap& io_bitmap() { return io_bitmap_; }

  static void ContextSwitch(X86ArchVmAspace* from, X86ArchVmAspace* to);

  // X86 has accessed and dirty flags on intermediate page table mappings, and not just terminal
  // page mappings. This means FreeUnaccessed is able to directly reclaim page tables without
  // needing to harvest individual page mappings via HarvestAccessed.
  static constexpr bool HasNonTerminalAccessedFlag() { return true; }

 private:
  // Test the vaddr against the address space's range.
  bool IsValidVaddr(vaddr_t vaddr) { return (vaddr >= base_ && vaddr <= base_ + size_ - 1); }

  fbl::Canary<fbl::magic("VAAS")> canary_;
  IoBitmap io_bitmap_;

  // Embedded storage for the object pointed to by |pt_|.
  union {
    alignas(X86PageTableMmu) char mmu[sizeof(X86PageTableMmu)];
    alignas(X86PageTableEpt) char ept[sizeof(X86PageTableEpt)];
  } page_table_storage_;

  // Page allocate function, if set will be used instead of the default allocator
  const page_alloc_fn_t test_page_alloc_func_ = nullptr;

  // This will be either a normal page table or an EPT, depending on whether
  // flags_ includes ARCH_ASPACE_FLAG_GUEST.
  X86PageTableBase* pt_;

  const uint flags_ = 0;

  // Range of address space.
  const vaddr_t base_ = 0;
  const size_t size_ = 0;

  // CPUs that are currently executing in this aspace.
  // Actually an mp_cpu_mask_t, but header dependencies.
  ktl::atomic<int> active_cpus_{0};
};

using ArchVmAspace = X86ArchVmAspace;

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_ASPACE_H_
