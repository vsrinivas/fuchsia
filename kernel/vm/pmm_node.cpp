// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "pmm_node.h"

#include <inttypes.h>
#include <kernel/mp.h>
#include <trace.h>
#include <vm/bootalloc.h>
#include <vm/physmap.h>
#include <zxcpp/new.h>

#include "vm_priv.h"

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

namespace {

void set_state_alloc(vm_page* page) {
    LTRACEF("page %p: prev state %s\n", page, page_state_to_string(page->state));

    DEBUG_ASSERT(page->state == VM_PAGE_STATE_FREE);

    page->state = VM_PAGE_STATE_ALLOC;
}

} // namespace

PmmNode::PmmNode() {
}

PmmNode::~PmmNode() {
}

// We disable thread safety analysis here, since this function is only called
// during early boot before threading exists.
zx_status_t PmmNode::AddArena(const pmm_arena_info_t* info) TA_NO_THREAD_SAFETY_ANALYSIS {
    LTRACEF("arena %p name '%s' base %#" PRIxPTR " size %#zx\n", info, info->name, info->base, info->size);

    // Make sure we're in early boot (ints disabled and no active CPUs according
    // to the scheduler).
    DEBUG_ASSERT(mp_get_active_mask() == 0);
    DEBUG_ASSERT(arch_ints_disabled());

    DEBUG_ASSERT(IS_PAGE_ALIGNED(info->base));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(info->size));
    DEBUG_ASSERT(info->size > 0);

    // allocate a c++ arena object
    PmmArena* arena = new (boot_alloc_mem(sizeof(PmmArena))) PmmArena();

    // initialize the object
    auto status = arena->Init(info, this);
    if (status != ZX_OK) {
        // leaks boot allocator memory
        arena->~PmmArena();
        printf("PMM: pmm_add_arena failed to initialize arena\n");
        return status;
    }

    // walk the arena list and add arena based on priority order
    for (auto& a : arena_list_) {
        if (a.priority() > arena->priority()) {
            arena_list_.insert(a, arena);
            goto done_add;
        }
    }

    // walked off the end, add it to the end of the list
    arena_list_.push_back(arena);

done_add:
    arena_cumulative_size_ += info->size;

    return ZX_OK;
}

// called at boot time as arenas are brought online, no locks are acquired
void PmmNode::AddFreePages(list_node* list) TA_NO_THREAD_SAFETY_ANALYSIS {
    LTRACEF("list %p\n", list);

    vm_page *temp, *page;
    list_for_every_entry_safe (list, page, temp, vm_page, queue_node) {
        list_delete(&page->queue_node);
        list_add_tail(&free_list_, &page->queue_node);
        free_count_++;
    }

    LTRACEF("free count now %" PRIu64 "\n", free_count_);
}

vm_page_t* PmmNode::AllocPage(uint alloc_flags, paddr_t* pa) {
    Guard<fbl::Mutex> guard{&lock_};

    vm_page* page = list_remove_head_type(&free_list_, vm_page, queue_node);
    if (!page)
        return nullptr;

    DEBUG_ASSERT(free_count_ > 0);

    free_count_--;

    DEBUG_ASSERT(page->is_free());

    set_state_alloc(page);

#if PMM_ENABLE_FREE_FILL
    CheckFreeFill(page);
#endif

    if (pa) {
        *pa = page->paddr();
    }

    LTRACEF("allocating page %p, pa %#" PRIxPTR "\n", page, page->paddr());

    return page;
}

size_t PmmNode::AllocPages(size_t count, uint alloc_flags, list_node* list) {
    LTRACEF("count %zu\n", count);

    // list must be initialized prior to calling this
    DEBUG_ASSERT(list);

    if (count == 0)
        return 0;

    Guard<fbl::Mutex> guard{&lock_};

    size_t allocated = 0;
    while (allocated < count) {
        vm_page* page = list_remove_head_type(&free_list_, vm_page, queue_node);
        if (!page)
            return allocated;

        LTRACEF("allocating page %p, pa %#" PRIxPTR "\n", page, page->paddr());

        DEBUG_ASSERT(free_count_ > 0);

        free_count_--;

        DEBUG_ASSERT(page->is_free());
#if PMM_ENABLE_FREE_FILL
        CheckFreeFill(page);
#endif

        page->state = VM_PAGE_STATE_ALLOC;
        list_add_tail(list, &page->queue_node);

        allocated++;
    }

    return allocated;
}

size_t PmmNode::AllocRange(paddr_t address, size_t count, list_node* list) {
    LTRACEF("address %#" PRIxPTR ", count %zu\n", address, count);

    size_t allocated = 0;
    if (count == 0)
        return 0;

    address = ROUNDDOWN(address, PAGE_SIZE);

    Guard<fbl::Mutex> guard{&lock_};

    // walk through the arenas, looking to see if the physical page belongs to it
    for (auto& a : arena_list_) {
        while (allocated < count && a.address_in_arena(address)) {
            vm_page_t* page = a.FindSpecific(address);
            if (!page)
                break;

            if (!page->is_free())
                break;

            list_delete(&page->queue_node);

            page->state = VM_PAGE_STATE_ALLOC;

            if (list)
                list_add_tail(list, &page->queue_node);

            allocated++;
            address += PAGE_SIZE;
            free_count_--;
        }

        if (allocated == count)
            break;
    }

    LTRACEF("returning allocated count %zu\n", allocated);

    return allocated;
}

size_t PmmNode::AllocContiguous(const size_t count, uint alloc_flags, uint8_t alignment_log2,
                                paddr_t* pa, list_node* list) {
    LTRACEF("count %zu, align %u\n", count, alignment_log2);

    if (count == 0)
        return 0;
    if (alignment_log2 < PAGE_SIZE_SHIFT)
        alignment_log2 = PAGE_SIZE_SHIFT;

    Guard<fbl::Mutex> guard{&lock_};

    for (auto& a : arena_list_) {
        vm_page_t* p = a.FindFreeContiguous(count, alignment_log2);
        if (!p)
            continue;

        if (pa)
            *pa = p->paddr();

        // remove the pages from the run out of the free list
        for (size_t i = 0; i < count; i++, p++) {
            DEBUG_ASSERT_MSG(p->is_free(), "p %p state %u\n", p, p->state);
            DEBUG_ASSERT(list_in_list(&p->queue_node));

            list_delete(&p->queue_node);
            p->state = VM_PAGE_STATE_ALLOC;

            DEBUG_ASSERT(free_count_ > 0);

            free_count_--;

#if PMM_ENABLE_FREE_FILL
            CheckFreeFill(p);
#endif

            if (list)
                list_add_tail(list, &p->queue_node);
        }

        return count;
    }

    LTRACEF("couldn't find run\n");
    return 0;
}

size_t PmmNode::Free(list_node* list) {
    LTRACEF("list %p\n", list);

    DEBUG_ASSERT(list);

    Guard<fbl::Mutex> guard{&lock_};

    uint count = 0;
    while (!list_is_empty(list)) {
        vm_page* page = list_remove_head_type(list, vm_page, queue_node);

        LTRACEF("page %p state %u\n", page, page->state);
        DEBUG_ASSERT(page->state != VM_PAGE_STATE_OBJECT || page->object.pin_count == 0);
        DEBUG_ASSERT(!page->is_free());

#if PMM_ENABLE_FREE_FILL
        FreeFill(page);
#endif

        // remove it from its old queue
        if (list_in_list(&page->queue_node))
            list_delete(&page->queue_node);

        // mark it free
        page->state = VM_PAGE_STATE_FREE;

        // add it to the free queue
        list_add_head(&free_list_, &page->queue_node);

        free_count_++;
        count++;
    }

    LTRACEF("returning count %u\n", count);

    return count;
}

void PmmNode::Free(vm_page* page) {
    LTRACEF("page %p, pa %#" PRIxPTR "\n", page, page->paddr());

    Guard<fbl::Mutex> guard{&lock_};

    DEBUG_ASSERT(page->state != VM_PAGE_STATE_OBJECT || page->object.pin_count == 0);
    DEBUG_ASSERT(!page->is_free());

#if PMM_ENABLE_FREE_FILL
    FreeFill(page);
#endif

    // remove it from its old queue
    if (list_in_list(&page->queue_node))
        list_delete(&page->queue_node);

    // mark it free
    page->state = VM_PAGE_STATE_FREE;

    // add it to the free queue
    list_add_head(&free_list_, &page->queue_node);

    free_count_++;
}

// okay if accessed outside of a lock
uint64_t PmmNode::CountFreePages() const TA_NO_THREAD_SAFETY_ANALYSIS {
    return free_count_;
}

uint64_t PmmNode::CountTotalBytes() const TA_NO_THREAD_SAFETY_ANALYSIS {
    return arena_cumulative_size_;
}

void PmmNode::CountTotalStates(uint64_t state_count[VM_PAGE_STATE_COUNT_]) const {
    // TODO(MG-833): This is extremely expensive, holding a global lock
    // and touching every page/arena. We should keep a running count instead.
    Guard<fbl::Mutex> guard{&lock_};
    for (auto& a : arena_list_) {
        a.CountStates(state_count);
    }
}

void PmmNode::DumpFree() const TA_NO_THREAD_SAFETY_ANALYSIS {
    auto megabytes_free = CountFreePages() / 256u;
    printf(" %zu free MBs\n", megabytes_free);
}

void PmmNode::Dump(bool is_panic) const {
    // No lock analysis here, as we want to just go for it in the panic case without the lock.
    auto dump = [this]() TA_NO_THREAD_SAFETY_ANALYSIS {
        printf("pmm node %p: free_count %zu (%zu bytes), total size %zu\n",
                this, free_count_, free_count_ * PAGE_SIZE, arena_cumulative_size_);
        for (auto& a : arena_list_) {
            a.Dump(false, false);
        }
    };

    if (is_panic) {
        dump();
    } else {
        Guard<fbl::Mutex> guard{&lock_};
        dump();
    }
}

#if PMM_ENABLE_FREE_FILL
void PmmNode::EnforceFill() {
    DEBUG_ASSERT(!enforce_fill_);

    vm_page* page;
    list_for_every_entry (&free_list_, page, vm_page, queue_node) {
        FreeFill(page);
    }

    enforce_fill_ = true;
}

void PmmNode::FreeFill(vm_page_t* page) {
    void* kvaddr = paddr_to_physmap(page->paddr());
    DEBUG_ASSERT(is_kernel_address((vaddr_t)kvaddr));
    memset(kvaddr, PMM_FREE_FILL_BYTE, PAGE_SIZE);
}

void PmmNode::CheckFreeFill(vm_page_t* page) {
    uint8_t* kvaddr = static_cast<uint8_t*>(paddr_to_physmap(page->paddr()));
    for (size_t j = 0; j < PAGE_SIZE; ++j) {
        ASSERT(!enforce_fill_ || *(kvaddr + j) == PMM_FREE_FILL_BYTE);
    }
}
#endif // PMM_ENABLE_FREE_FILL
