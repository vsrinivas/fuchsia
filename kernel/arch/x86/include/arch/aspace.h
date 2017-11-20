// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/x86/ioport.h>
#include <arch/x86/mmu.h>
#include <fbl/algorithm.h>
#include <fbl/atomic.h>
#include <fbl/canary.h>
#include <fbl/mutex.h>
#include <vm/arch_vm_aspace.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

struct MappingCursor;

class X86PageTableBase {
public:
    X86PageTableBase();
    virtual ~X86PageTableBase();

    // Type for flags used in the hardware page tables, for terminal entries.
    // Note that some flags here may have meanings that depend on the level
    // at which they occur (e.g. page size and PAT).
    using PtFlags = uint64_t;

    // Type for flags used in the hardware page tables, for non-terminal
    // entries.
    using IntermediatePtFlags = uint64_t;

    // Initialize an empty page table, assigning this given context to it.
    zx_status_t Init(void* ctx);

    // Release the resources associated with this page table.  |base| and |size|
    // are only used for debug checks that the page tables have no more mappings.
    zx_status_t Destroy(vaddr_t base, size_t size);

    paddr_t phys() const { return phys_; }
    void* virt() const { return virt_; }

    size_t pages() const { return pages_; }
    void* ctx() const { return ctx_; }

    zx_status_t MapPages(vaddr_t vaddr, paddr_t paddr, const size_t count,
                         uint flags, size_t* mapped);
    zx_status_t UnmapPages(vaddr_t vaddr, const size_t count, size_t* unmapped);
    zx_status_t ProtectPages(vaddr_t vaddr, size_t count, uint flags);

    template <typename F>
    zx_status_t QueryVaddr(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags,
                           F pt_flags_to_mmu);

    // Return the page size for this level
    // TODO(teisenbe): This would probably be better elsewhere.
    static size_t page_size(page_table_levels level) {
        switch (level) {
        case PT_L:
            return 1ULL << PT_SHIFT;
        case PD_L:
            return 1ULL << PD_SHIFT;
        case PDP_L:
            return 1ULL << PDP_SHIFT;
        case PML4_L:
            return 1ULL << PML4_SHIFT;
        default:
            panic("page_size: invalid level\n");
        }
    }

protected:
    // Whether the processor supports the page size of this level
    virtual bool supports_page_size(page_table_levels level) = 0;
    // Return the hardware flags to use on intermediate page tables entries
    virtual IntermediatePtFlags intermediate_flags() = 0;
    // Return the hardware flags to use on terminal page table entries
    virtual PtFlags terminal_flags(page_table_levels level, uint flags) = 0;
    // Return the hardware flags to use on smaller pages after a splitting a
    // large page with flags |flags|.
    virtual PtFlags split_flags(page_table_levels level, PtFlags flags) = 0;
    // Invalidate a single page at the given level
    virtual void TlbInvalidatePage(page_table_levels level, X86PageTableBase* pt, vaddr_t vaddr,
                                   bool global_page) = 0;

    // Pointer to the translation table.
    paddr_t phys_ = 0;
    pt_entry_t* virt_ = nullptr;

    // Counter of pages allocated to back the translation table.
    size_t pages_ = 0;

    // A context structure that may used by a PageTable type above as part of
    // invalidation.
    void* ctx_ = nullptr;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(X86PageTableBase);

    // Whether an address is aligned to the page size of this level
    bool page_aligned(page_table_levels level, vaddr_t vaddr) {
        return (vaddr & (page_size(level) - 1)) == 0;
    }
    // Extract the index needed for finding |vaddr| for the given level
    uint vaddr_to_index(page_table_levels level, vaddr_t vaddr) {
        switch (level) {
        case PML4_L:
            return VADDR_TO_PML4_INDEX(vaddr);
        case PDP_L:
            return VADDR_TO_PDP_INDEX(vaddr);
        case PD_L:
            return VADDR_TO_PD_INDEX(vaddr);
        case PT_L:
            return VADDR_TO_PT_INDEX(vaddr);
        default:
            panic("vaddr_to_index: invalid level\n");
        }
    }
    // Convert a PTE to a physical address
    paddr_t paddr_from_pte(page_table_levels level, pt_entry_t pte) {
        DEBUG_ASSERT(IS_PAGE_PRESENT(pte));

        paddr_t pa;
        switch (level) {
        case PDP_L:
            pa = (pte & X86_HUGE_PAGE_FRAME);
            break;
        case PD_L:
            pa = (pte & X86_LARGE_PAGE_FRAME);
            break;
        case PT_L:
            pa = (pte & X86_PG_FRAME);
            break;
        default:
            panic("paddr_from_pte at unhandled level %d\n", level);
        }

        return pa;
    }

    zx_status_t AddMapping(volatile pt_entry_t* table, uint mmu_flags,
                           page_table_levels level, const MappingCursor& start_cursor,
                           MappingCursor* new_cursor) TA_REQ(lock_);
    zx_status_t AddMappingL0(volatile pt_entry_t* table, uint mmu_flags,
                             const MappingCursor& start_cursor,
                             MappingCursor* new_cursor) TA_REQ(lock_);

    bool RemoveMapping(volatile pt_entry_t* table,
                       page_table_levels level, const MappingCursor& start_cursor,
                       MappingCursor* new_cursor) TA_REQ(lock_);
    bool RemoveMappingL0(volatile pt_entry_t* table,
                         const MappingCursor& start_cursor,
                         MappingCursor* new_cursor) TA_REQ(lock_);

    zx_status_t UpdateMapping(volatile pt_entry_t* table, uint mmu_flags,
                              page_table_levels level, const MappingCursor& start_cursor,
                              MappingCursor* new_cursor) TA_REQ(lock_);
    zx_status_t UpdateMappingL0(volatile pt_entry_t* table, uint mmu_flags,
                                const MappingCursor& start_cursor,
                                MappingCursor* new_cursor) TA_REQ(lock_);

    zx_status_t GetMapping(volatile pt_entry_t* table, vaddr_t vaddr,
                           page_table_levels level,
                           page_table_levels* ret_level,
                           volatile pt_entry_t** mapping) TA_REQ(lock_);
    zx_status_t GetMappingL0(volatile pt_entry_t* table, vaddr_t vaddr,
                             enum page_table_levels* ret_level,
                             volatile pt_entry_t** mapping) TA_REQ(lock_);

    void UpdateEntry(page_table_levels level, vaddr_t vaddr, volatile pt_entry_t* pte,
                     paddr_t paddr, PtFlags flags) TA_REQ(lock_);

    zx_status_t SplitLargePage(page_table_levels level, vaddr_t vaddr,
                               volatile pt_entry_t* pte) TA_REQ(lock_);

    void UnmapEntry(page_table_levels level, vaddr_t vaddr, volatile pt_entry_t* pte) TA_REQ(lock_);

    fbl::Canary<fbl::magic("X86P")> canary_;

    // low lock to protect the mmu code
    fbl::Mutex lock_;
};

// Implementation of page tables used by x86-64 CPUs.
class X86PageTableMmu final : public X86PageTableBase {
public:
    // Initialize the kernel page table, assigning the given context to it.
    // This X86PageTable will be special in that its mappings will all have
    // the G (global) bit set, and are expected to be aliased across all page
    // tables used in the normal MMU.  See |AliasKernelMappings|.
    zx_status_t InitKernel(void* ctx);

    // Used for normal MMU page tables so they can share the high kernel mapping
    zx_status_t AliasKernelMappings();

private:
    bool supports_page_size(page_table_levels level) final;
    IntermediatePtFlags intermediate_flags() final;
    PtFlags terminal_flags(page_table_levels level, uint flags) final;
    PtFlags split_flags(page_table_levels level, PtFlags flags) final;
    void TlbInvalidatePage(page_table_levels level, X86PageTableBase* pt, vaddr_t vaddr,
                           bool global_page) final;

    // If true, all mappings will have the global bit set.
    bool use_global_mappings_ = false;
};

// Implementation of Intel's Extended Page Tables, for use in virtualization.
class X86PageTableEpt final : public X86PageTableBase {
private:
    bool supports_page_size(page_table_levels level) final;
    IntermediatePtFlags intermediate_flags() final;
    PtFlags terminal_flags(page_table_levels level, uint flags) final;
    PtFlags split_flags(page_table_levels level, PtFlags flags) final;
    void TlbInvalidatePage(page_table_levels level, X86PageTableBase* pt, vaddr_t vaddr,
                           bool global_page) final;
};

class X86ArchVmAspace final : public ArchVmAspaceInterface {
public:
    X86ArchVmAspace();
    virtual ~X86ArchVmAspace();

    zx_status_t Init(vaddr_t base, size_t size, uint mmu_flags) override;

    zx_status_t Destroy() override;

    // main methods
    zx_status_t Map(vaddr_t vaddr, paddr_t paddr, size_t count,
                    uint mmu_flags, size_t* mapped) override;
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
