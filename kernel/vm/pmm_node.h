// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>

#include <kernel/lockdep.h>
#include <kernel/mutex.h>
#include <vm/pmm.h>

#include "pmm_arena.h"

#define PMM_ENABLE_FREE_FILL 0
#define PMM_FREE_FILL_BYTE 0x42

// per numa node collection of pmm arenas and worker threads
class PmmNode {
public:
    PmmNode();
    ~PmmNode();

    DISALLOW_COPY_ASSIGN_AND_MOVE(PmmNode);

    paddr_t PageToPaddr(const vm_page_t* page) TA_NO_THREAD_SAFETY_ANALYSIS;
    vm_page_t* PaddrToPage(paddr_t addr) TA_NO_THREAD_SAFETY_ANALYSIS;

    // main allocator routines
    vm_page_t* AllocPage(uint alloc_flags, paddr_t* pa);
    size_t AllocPages(size_t count, uint alloc_flags, list_node* list);
    size_t AllocRange(paddr_t address, size_t count, list_node* list);
    size_t AllocContiguous(size_t count, uint alloc_flags, uint8_t alignment_log2, paddr_t* pa, list_node* list);
    void Free(vm_page* page);
    size_t Free(list_node* list);

    uint64_t CountFreePages() const;
    uint64_t CountTotalBytes() const;
    void CountTotalStates(uint64_t state_count[VM_PAGE_STATE_COUNT_]) const;

    // printf free and overall state of the internal arenas
    // NOTE: both functions skip mutexes and can be called inside timer or crash context
    // though the data they return may be questionable
    void DumpFree() const TA_NO_THREAD_SAFETY_ANALYSIS;
    void Dump(bool is_panic) const TA_NO_THREAD_SAFETY_ANALYSIS;

#if PMM_ENABLE_FREE_FILL
    void EnforceFill() TA_NO_THREAD_SAFETY_ANALYSIS;
#endif

    zx_status_t AddArena(const pmm_arena_info_t* info);

    // add new pages to the free queue. used when boostrapping a PmmArena
    void AddFreePages(list_node *list);

private:
    fbl::Canary<fbl::magic("PNOD")> canary_;

    mutable DECLARE_MUTEX(PmmNode) lock_;

    uint64_t arena_cumulative_size_ TA_GUARDED(lock_) = 0;
    uint64_t free_count_ TA_GUARDED(lock_) = 0;

    fbl::DoublyLinkedList<PmmArena*> arena_list_ TA_GUARDED(lock_);

    // page queues
    list_node free_list_ TA_GUARDED(lock_) = LIST_INITIAL_VALUE(free_list_);
    list_node inactive_list_ TA_GUARDED(lock_) = LIST_INITIAL_VALUE(inactive_list_);
    list_node active_list_ TA_GUARDED(lock_) = LIST_INITIAL_VALUE(active_list_);
    list_node modified_list_ TA_GUARDED(lock_) = LIST_INITIAL_VALUE(modified_list_);
    list_node wired_list_ TA_GUARDED(lock_) = LIST_INITIAL_VALUE(wired_list_);

#if PMM_ENABLE_FREE_FILL
    void FreeFill(vm_page_t* page);
    void CheckFreeFill(vm_page_t* page);

    bool enforce_fill_ = false;
#endif

};

// We don't need to hold the arena lock while executing this, since it is
// only accesses values that are set once during system initialization.
inline vm_page_t* PmmNode::PaddrToPage(paddr_t addr) TA_NO_THREAD_SAFETY_ANALYSIS {
    for (auto& a : arena_list_) {
        if (a.address_in_arena(addr)) {
            size_t index = (addr - a.base()) / PAGE_SIZE;
            return a.get_page(index);
        }
    }
    return nullptr;
}
