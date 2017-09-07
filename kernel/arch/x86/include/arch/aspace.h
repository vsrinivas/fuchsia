// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/x86/ioport.h>
#include <arch/x86/mmu.h>
#include <kernel/atomic.h>
#include <vm/arch_vm_aspace.h>
#include <magenta/compiler.h>
#include <fbl/canary.h>
#include <fbl/mutex.h>

struct MappingCursor;

class X86ArchVmAspace final : public ArchVmAspaceInterface {
public:
    template <typename PageTable>
    static void UnmapEntry(X86ArchVmAspace* aspace, vaddr_t vaddr, volatile pt_entry_t* pte);

    X86ArchVmAspace();
    virtual ~X86ArchVmAspace();

    status_t Init(vaddr_t base, size_t size, uint mmu_flags) override;

    status_t Destroy() override;

    // main methods
    status_t Map(vaddr_t vaddr, paddr_t paddr, size_t count,
                 uint mmu_flags, size_t* mapped) override;

    status_t Unmap(vaddr_t vaddr, size_t count, size_t* unmapped) override;

    status_t Protect(vaddr_t vaddr, size_t count, uint mmu_flags) override;

    status_t Query(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) override;

    vaddr_t PickSpot(vaddr_t base, uint prev_region_mmu_flags,
                     vaddr_t end, uint next_region_mmu_flags,
                     vaddr_t align, size_t size, uint mmu_flags) override;

    paddr_t arch_table_phys() const override { return pt_phys_; }

    paddr_t pt_phys() const { return pt_phys_; }

    size_t pt_pages() const { return pt_pages_; }

    int active_cpus() { return atomic_load(&active_cpus_); }

    IoBitmap& io_bitmap() { return io_bitmap_; }

    static void ContextSwitch(X86ArchVmAspace* from, X86ArchVmAspace* to);

private:
    // Test the vaddr against the address space's range.
    bool IsValidVaddr(vaddr_t vaddr) {
        return (vaddr >= base_ && vaddr <= base_ + size_ - 1);
    }

    template <template <int> class PageTable>
    status_t DestroyAspace() TA_REQ(lock_);

    template <template <int> class PageTable>
    status_t MapPages(vaddr_t vaddr, paddr_t paddr, const size_t count,
                      uint mmu_flags, size_t* mapped) TA_REQ(lock_);

    template <template <int> class PageTable>
    status_t UnmapPages(vaddr_t vaddr, const size_t count, size_t* unmapped) TA_REQ(lock_);

    template <template <int> class PageTable>
    status_t ProtectPages(vaddr_t vaddr, size_t count, uint mmu_flags) TA_REQ(lock_);

    template <template <int> class PageTable, typename F>
    status_t QueryVaddr(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags,
                        F arch_to_mmu) TA_REQ(lock_);

    template <typename PageTable>
    status_t AddMapping(volatile pt_entry_t* table, uint mmu_flags,
                        const MappingCursor& start_cursor,
                        MappingCursor* new_cursor) TA_REQ(lock_);

    template <typename PageTable>
    status_t AddMappingL0(volatile pt_entry_t* table, uint mmu_flags,
                          const MappingCursor& start_cursor,
                          MappingCursor* new_cursor) TA_REQ(lock_);

    template <typename PageTable>
    bool RemoveMapping(volatile pt_entry_t* table,
                       const MappingCursor& start_cursor,
                       MappingCursor* new_cursor) TA_REQ(lock_);
    template <typename PageTable>
    bool RemoveMappingL0(volatile pt_entry_t* table,
                         const MappingCursor& start_cursor,
                         MappingCursor* new_cursor) TA_REQ(lock_);

    template <typename PageTable>
    status_t UpdateMapping(volatile pt_entry_t* table, uint mmu_flags,
                           const MappingCursor& start_cursor,
                           MappingCursor* new_cursor) TA_REQ(lock_);

    template <typename PageTable>
    status_t UpdateMappingL0(volatile pt_entry_t* table, uint mmu_flags,
                             const MappingCursor& start_cursor,
                             MappingCursor* new_cursor) TA_REQ(lock_);

    template <typename PageTable>
    status_t GetMapping(volatile pt_entry_t* table, vaddr_t vaddr,
                        page_table_levels* ret_level,
                        volatile pt_entry_t** mapping) TA_REQ(lock_);

    template <typename PageTable>
    status_t GetMappingL0(volatile pt_entry_t* table, vaddr_t vaddr,
                          enum page_table_levels* ret_level,
                          volatile pt_entry_t** mapping) TA_REQ(lock_);

    template <typename PageTable>
    void UpdateEntry(vaddr_t vaddr, volatile pt_entry_t* pte, paddr_t paddr,
                     arch_flags_t flags) TA_REQ(lock_);

    template <typename PageTable>
    status_t SplitLargePage(vaddr_t vaddr, volatile pt_entry_t* pte) TA_REQ(lock_);

    fbl::Canary<fbl::magic("VAAS")> canary_;
    IoBitmap io_bitmap_;

    // low lock to protect the mmu code
    fbl::Mutex lock_;

    // Pointer to the translation table.
    paddr_t pt_phys_ = 0;
    pt_entry_t* pt_virt_ = nullptr;

    // Counter of pages allocated to back the translation table.
    size_t pt_pages_ = 0;

    uint flags_ = 0;

    // Range of address space.
    vaddr_t base_ = 0;
    size_t size_ = 0;

    // CPUs that are currently executing in this aspace.
    // Actually an mp_cpu_mask_t, but header dependencies.
    volatile int active_cpus_ = 0;
};

using ArchVmAspace = X86ArchVmAspace;
