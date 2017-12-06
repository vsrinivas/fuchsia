// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/canary.h>
#include <fbl/mutex.h>

typedef uint64_t pt_entry_t;
#define PRIxPTE PRIx64

struct MappingCursor;

/* Different page table levels in the page table mgmt hirerachy */
enum page_table_levels {
    PT_L,
    PD_L,
    PDP_L,
    PML4_L,
};

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

    paddr_t phys() const { return phys_; }
    void* virt() const { return virt_; }

    size_t pages() const { return pages_; }
    void* ctx() const { return ctx_; }

    zx_status_t MapPages(vaddr_t vaddr, paddr_t* phys, size_t count,
                         uint flags, size_t* mapped);
    zx_status_t MapPagesContiguous(vaddr_t vaddr, paddr_t paddr, const size_t count,
                                   uint flags, size_t* mapped);
    zx_status_t UnmapPages(vaddr_t vaddr, const size_t count, size_t* unmapped);
    zx_status_t ProtectPages(vaddr_t vaddr, size_t count, uint flags);

    zx_status_t QueryVaddr(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags);

protected:
    // Initialize an empty page table, assigning this given context to it.
    zx_status_t Init(void* ctx);

    // Release the resources associated with this page table.  |base| and |size|
    // are only used for debug checks that the page tables have no more mappings.
    zx_status_t Destroy(vaddr_t base, size_t size);

    // Returns the highest level of the page tables
    virtual page_table_levels top_level() = 0;
    // Returns true if the given ARCH_MMU_FLAG_* flag combination is valid.
    virtual bool allowed_flags(uint flags) = 0;
    // Returns true if the given paddr is valid
    virtual bool check_paddr(paddr_t paddr) = 0;
    // Returns true if the given vaddr is valid
    virtual bool check_vaddr(vaddr_t vaddr) = 0;
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
    virtual void TlbInvalidatePage(page_table_levels level, vaddr_t vaddr, bool global_page) = 0;
    // Convert PtFlags to ARCH_MMU_* flags.
    virtual uint pt_flags_to_mmu_flags(PtFlags flags, page_table_levels level) = 0;
    // Returns true if a cache flush is necessary for pagetable changes to be
    // visible.
    virtual bool needs_cache_flushes() = 0;

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
