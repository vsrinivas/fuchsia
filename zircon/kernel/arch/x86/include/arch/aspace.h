// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/x86/ioport.h>
#include <arch/x86/mmu.h>
#include <arch/x86/page_tables/page_tables.h>
#include <fbl/algorithm.h>
#include <fbl/atomic.h>
#include <fbl/canary.h>
#include <vm/arch_vm_aspace.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// Implementation of page tables used by x86-64 CPUs.
class X86PageTableMmu final : public X86PageTableBase {
public:
    using X86PageTableBase::Init;
    using X86PageTableBase::Destroy;

    // Initialize the kernel page table, assigning the given context to it.
    // This X86PageTable will be special in that its mappings will all have
    // the G (global) bit set, and are expected to be aliased across all page
    // tables used in the normal MMU.  See |AliasKernelMappings|.
    zx_status_t InitKernel(void* ctx);

    // Used for normal MMU page tables so they can share the high kernel mapping
    zx_status_t AliasKernelMappings();

private:
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
    using X86PageTableBase::Init;
    using X86PageTableBase::Destroy;
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
    X86ArchVmAspace();
    virtual ~X86ArchVmAspace();

    zx_status_t Init(vaddr_t base, size_t size, uint mmu_flags) override;

    zx_status_t Destroy() override;

    // main methods
    zx_status_t MapContiguous(vaddr_t vaddr, paddr_t paddr, size_t count,
                              uint mmu_flags, size_t* mapped) override;
    zx_status_t Map(vaddr_t vaddr, paddr_t* phys, size_t count, uint mmu_flags,
                    size_t* mapped) override;
    zx_status_t Unmap(vaddr_t vaddr, size_t count, size_t* unmapped) override;
    zx_status_t Protect(vaddr_t vaddr, size_t count, uint mmu_flags) override;
    zx_status_t Query(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) override;

    vaddr_t PickSpot(vaddr_t base, uint prev_region_mmu_flags,
                     vaddr_t end, uint next_region_mmu_flags,
                     vaddr_t align, size_t size, uint mmu_flags) override;

    paddr_t arch_table_phys() const override { return pt_->phys(); }
    paddr_t pt_phys() const { return pt_->phys(); }
    size_t pt_pages() const { return pt_->pages(); }

    int active_cpus() { return active_cpus_.load(); }

    IoBitmap& io_bitmap() { return io_bitmap_; }

    static void ContextSwitch(X86ArchVmAspace* from, X86ArchVmAspace* to);

private:
    // Test the vaddr against the address space's range.
    bool IsValidVaddr(vaddr_t vaddr) {
        return (vaddr >= base_ && vaddr <= base_ + size_ - 1);
    }

    fbl::Canary<fbl::magic("VAAS")> canary_;
    IoBitmap io_bitmap_;

    static constexpr size_t kPageTableAlign = fbl::max(alignof(X86PageTableMmu),
                                                       alignof(X86PageTableEpt));
    static constexpr size_t kPageTableSize = fbl::max(sizeof(X86PageTableMmu),
                                                      sizeof(X86PageTableEpt));
    // Embedded storage for the object pointed to by |pt_|.
    alignas(kPageTableAlign) char page_table_storage_[kPageTableSize];

    // This will be either a normal page table or an EPT, depending on whether
    // flags_ includes ARCH_ASPACE_FLAG_GUEST.
    X86PageTableBase* pt_;

    uint flags_ = 0;

    // Range of address space.
    vaddr_t base_ = 0;
    size_t size_ = 0;

    // CPUs that are currently executing in this aspace.
    // Actually an mp_cpu_mask_t, but header dependencies.
    fbl::atomic_int active_cpus_{0};
};

using ArchVmAspace = X86ArchVmAspace;
