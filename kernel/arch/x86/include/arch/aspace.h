// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/x86/ioport.h>
#include <arch/x86/mmu.h>
#include <vm/arch_vm_aspace.h>
#include <fbl/atomic.h>
#include <fbl/canary.h>
#include <fbl/mutex.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

struct MappingCursor;

class X86PageTable {
public:
    X86PageTable();
    ~X86PageTable();

    // Initialize an empty page table, assigning this given context to it.
    zx_status_t Init(void* ctx);

    // Initialize the kernel page table, assigning the given context to it.
    // This X86PageTable will be special in that its mappings will all have
    // the G (global) bit set, and are expected to be aliased across all page
    // tables used in the normal MMU.  See |AliasKernelMappings|.
    zx_status_t InitKernel(void* ctx);

    // Used for normal MMU page tables so they can share the high kernel mapping
    zx_status_t AliasKernelMappings();

    // Release the resources associated with this page table.  |base| and |size|
    // are only used for debug checks that the page tables have no more mappings.
    template <typename PageTable>
    zx_status_t Destroy(vaddr_t base, size_t size);

    paddr_t phys() const { return phys_; }
    void* virt() const { return virt_; }

    size_t pages() const { return pages_; }
    void* ctx() const { return ctx_; }

    template <typename PageTable>
    zx_status_t MapPages(vaddr_t vaddr, paddr_t paddr, const size_t count,
                         uint mmu_flags, size_t* mapped);
    template <typename PageTable>
    zx_status_t UnmapPages(vaddr_t vaddr, const size_t count, size_t* unmapped);
    template <typename PageTable>
    zx_status_t ProtectPages(vaddr_t vaddr, size_t count, uint mmu_flags);

    template <typename PageTable, typename F>
    zx_status_t QueryVaddr(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags,
                           F arch_to_mmu);
private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(X86PageTable);

    template <typename PageTable>
    zx_status_t AddMapping(volatile pt_entry_t* table, uint mmu_flags,
                           page_table_levels level, const MappingCursor& start_cursor,
                           MappingCursor* new_cursor) TA_REQ(lock_);
    template <typename PageTable>
    zx_status_t AddMappingL0(volatile pt_entry_t* table, uint mmu_flags,
                             const MappingCursor& start_cursor,
                             MappingCursor* new_cursor) TA_REQ(lock_);

    template <typename PageTable>
    bool RemoveMapping(volatile pt_entry_t* table,
                       page_table_levels level, const MappingCursor& start_cursor,
                       MappingCursor* new_cursor) TA_REQ(lock_);
    template <typename PageTable>
    bool RemoveMappingL0(volatile pt_entry_t* table,
                         const MappingCursor& start_cursor,
                         MappingCursor* new_cursor) TA_REQ(lock_);

    template <typename PageTable>
    zx_status_t UpdateMapping(volatile pt_entry_t* table, uint mmu_flags,
                              page_table_levels level, const MappingCursor& start_cursor,
                              MappingCursor* new_cursor) TA_REQ(lock_);
    template <typename PageTable>
    zx_status_t UpdateMappingL0(volatile pt_entry_t* table, uint mmu_flags,
                                const MappingCursor& start_cursor,
                                MappingCursor* new_cursor) TA_REQ(lock_);

    template <typename PageTable>
    zx_status_t GetMapping(volatile pt_entry_t* table, vaddr_t vaddr,
                           page_table_levels level,
                           page_table_levels* ret_level,
                           volatile pt_entry_t** mapping) TA_REQ(lock_);

    template <typename PageTable>
    zx_status_t GetMappingL0(volatile pt_entry_t* table, vaddr_t vaddr,
                             enum page_table_levels* ret_level,
                             volatile pt_entry_t** mapping) TA_REQ(lock_);

    template <typename PageTable>
    void UpdateEntry(page_table_levels level, vaddr_t vaddr, volatile pt_entry_t* pte,
                     paddr_t paddr, arch_flags_t flags) TA_REQ(lock_);

    template <typename PageTable>
    zx_status_t SplitLargePage(page_table_levels level, vaddr_t vaddr,
                               volatile pt_entry_t* pte) TA_REQ(lock_);

    template <typename PageTable>
    void UnmapEntry(page_table_levels level, vaddr_t vaddr, volatile pt_entry_t* pte) TA_REQ(lock_);

    fbl::Canary<fbl::magic("X86P")> canary_;

    // Pointer to the translation table.
    paddr_t phys_ = 0;
    pt_entry_t* virt_ = nullptr;

    // A context structure that may used by a PageTable type above as part of
    // invalidation.
    void* ctx_ = nullptr;

    // Counter of pages allocated to back the translation table.
    size_t pages_ = 0;

    // low lock to protect the mmu code
    fbl::Mutex lock_;

    // If true, all mappings will have the global bit set.
    bool use_global_mappings_ = false;
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

    paddr_t arch_table_phys() const override { return pt_.phys(); }
    paddr_t pt_phys() const { return pt_.phys(); }
    size_t pt_pages() const { return pt_.pages(); }

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

    X86PageTable pt_;

    uint flags_ = 0;

    // Range of address space.
    vaddr_t base_ = 0;
    size_t size_ = 0;

    // CPUs that are currently executing in this aspace.
    // Actually an mp_cpu_mask_t, but header dependencies.
    fbl::atomic_int active_cpus_{0};
};

using ArchVmAspace = X86ArchVmAspace;
