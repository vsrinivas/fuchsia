// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <mxtl/arena.h>

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>

#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object_paged.h>
#include <mxtl/auto_call.h>
#include <mxcpp/new.h>

#define LOCAL_TRACE 0

namespace mxtl {

Arena::~Arena() {
    free_.clear();
    if (vmar_ != nullptr) {
        // Unmap all of our memory.
        // The VMAR's parent holds a ref, so it won't be destroyed
        // automatically when we're destroyed.
        vmar_->Destroy();
        vmar_.reset();
    }
}

status_t Arena::Init(const char* name, size_t ob_size, size_t count) {
    if ((ob_size == 0) || (ob_size > PAGE_SIZE))
        return ERR_INVALID_ARGS;
    if (!count)
        return ERR_INVALID_ARGS;
    LTRACEF("Arena '%s': ob_size %zu, count %zu\n", name, ob_size, count);

    // Carve out the memory:
    // - Kernel root VMAR
    //   + Sub VMAR
    //     + Control pool mapping
    //     + Unmapped guard page
    //     + Data pool mapping
    // Both mappings are backed by a single VMO.
    const size_t control_mem_sz = ROUNDUP(count * sizeof(Node), PAGE_SIZE);
    const size_t data_mem_sz = ROUNDUP(count * ob_size, PAGE_SIZE);
    const size_t guard_sz = PAGE_SIZE;
    const size_t vmo_sz = control_mem_sz + data_mem_sz;
    const size_t vmar_sz = vmo_sz + guard_sz;

    // Create the VMO.
    mxtl::RefPtr<VmObject> vmo =
        VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, vmo_sz);
    if (vmo == nullptr) {
        LTRACEF("Arena '%s': can't create %zu-byte VMO\n", name, vmo_sz);
        return ERR_NO_MEMORY;
    }

    auto kspace = VmAspace::kernel_aspace();
    DEBUG_ASSERT(kspace != nullptr);
    auto root_vmar = kspace->RootVmar();
    DEBUG_ASSERT(root_vmar != nullptr);

    char vname[32];
    snprintf(vname, sizeof(vname), "arena:%s", name);

    // Create the VMAR.
    mxtl::RefPtr<VmAddressRegion> vmar;
    status_t st = root_vmar->CreateSubVmar(0, // offset (ignored)
                                           vmar_sz,
                                           false, // align_pow2
                                           VMAR_FLAG_CAN_MAP_READ |
                                               VMAR_FLAG_CAN_MAP_WRITE |
                                               VMAR_FLAG_CAN_MAP_SPECIFIC,
                                           vname, &vmar);
    if (st != NO_ERROR || vmar == nullptr) {
        LTRACEF("Arena '%s': can't create %zu-byte VMAR (%d)\n",
                name, vmar_sz, st);
        return ERR_NO_MEMORY;
    }
    // The VMAR's parent holds a ref, so it won't be destroyed
    // automatically when we return.
    auto destroy_vmar = mxtl::MakeAutoCall([&vmar](){ vmar->Destroy(); });

    // Create a mapping for the control pool.
    mxtl::RefPtr<VmMapping> control_mapping;
    st = vmar->CreateVmMapping(0, // mapping_offset
                               control_mem_sz,
                               false, // align_pow2
                               VMAR_FLAG_SPECIFIC,
                               vmo,
                               0, // vmo_offset
                               ARCH_MMU_FLAG_PERM_READ |
                                   ARCH_MMU_FLAG_PERM_WRITE,
                               "control", &control_mapping);
    if (st != NO_ERROR || control_mapping == nullptr) {
        LTRACEF("Arena '%s': can't create %zu-byte control mapping (%d)\n",
                name, control_mem_sz, st);
        return ERR_NO_MEMORY;
    }

    // Create a mapping for the data pool, leaving an unmapped gap
    // between it and the control pool.
    mxtl::RefPtr<VmMapping> data_mapping;
    st = vmar->CreateVmMapping(control_mem_sz + guard_sz, // mapping_offset
                               data_mem_sz,
                               false, // align_pow2
                               VMAR_FLAG_SPECIFIC,
                               vmo,
                               control_mem_sz, // vmo_offset
                               ARCH_MMU_FLAG_PERM_READ |
                                   ARCH_MMU_FLAG_PERM_WRITE,
                               "data", &data_mapping);
    if (st != NO_ERROR || data_mapping == nullptr) {
        LTRACEF("Arena '%s': can't create %zu-byte data mapping (%d)\n",
                name, data_mem_sz, st);
        return ERR_NO_MEMORY;
    }

    // TODO(dbort): Add a VmMapping flag that says "do not demand page",
    // requiring and ensuring that we commit our pages manually.

    control_.Init(control_mapping, sizeof(Node));
    data_.Init(data_mapping, ob_size);

    vmar_ = vmar;
    destroy_vmar.cancel();

    if (LOCAL_TRACE) {
        Dump();
    }
    return NO_ERROR;
}

void Arena::Pool::Init(mxtl::RefPtr<VmMapping> mapping, size_t slot_size) {
    DEBUG_ASSERT(mapping != nullptr);
    DEBUG_ASSERT(slot_size > 0);

    slot_size_ = slot_size;
    mapping_ = mapping;
    committed_max_ = committed_ = top_ = start_ =
        reinterpret_cast<char*>(mapping_->base());
    end_ = start_ + mapping_->size();

    DEBUG_ASSERT(IS_PAGE_ALIGNED(start_));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(end_));
}

// Pick values that avoid lots of commits + decommits when
// right on the edge.
static const size_t kPoolCommitIncrease = 4 * PAGE_SIZE;
static const size_t kPoolDecommitThreshold = 8 * PAGE_SIZE;
static_assert(kPoolCommitIncrease < kPoolDecommitThreshold, "");

void* Arena::Pool::Pop() {
    if (static_cast<size_t>(end_ - top_) < slot_size_) {
        LTRACEF("%s: no room\n", mapping_->name());
        return nullptr;
    }
    if (top_ + slot_size_ > committed_) {
        // We've hit the end of our committed pages; commit some more.
        char* nc = committed_ + kPoolCommitIncrease;
        if (nc > end_) {
            nc = end_;
        }
        LTRACEF("%s: commit 0x%p..0x%p\n", mapping_->name(), committed_, nc);
        const size_t offset =
            reinterpret_cast<vaddr_t>(committed_) - mapping_->base();
        const size_t len = nc - committed_;
        status_t st = mapping_->MapRange(offset, len, /* commit */ true);
        if (st != NO_ERROR) {
            LTRACEF("%s: can't map range 0x%p..0x%p: %d\n",
                    mapping_->name(), committed_, nc, st);
            // Try to clean up any committed pages, but don't require
            // that it succeeds.
            mapping_->DecommitRange(offset, len, /* decommitted */ nullptr);
            return nullptr;
        }
        committed_ = nc;
        if (committed_ > committed_max_) {
            committed_max_ = committed_;
        }
    }
    char* slot = top_;
    top_ += slot_size_;
    return slot;
}

void Arena::Pool::Push(void* p) {
    // Can only push the most-recently-popped slot.
    ASSERT(reinterpret_cast<char*>(p) + slot_size_ == top_);
    top_ -= slot_size_;
    if (static_cast<size_t>(committed_ - top_) >= kPoolDecommitThreshold) {
        char* nc = reinterpret_cast<char*>(
            ROUNDUP(reinterpret_cast<uintptr_t>(top_ + kPoolCommitIncrease),
                    PAGE_SIZE));
        if (nc > end_) {
            nc = end_;
        }
        if (nc >= committed_) {
            return;
        }
        LTRACEF("%s: decommit 0x%p..0x%p\n", mapping_->name(), nc, committed_);
        const size_t offset = reinterpret_cast<vaddr_t>(nc) - mapping_->base();
        const size_t len = committed_ - nc;
        // If this fails or decommits less than we asked for, oh well.
        mapping_->DecommitRange(offset, len, /* decommitted */ nullptr);
        committed_ = nc;
    }
}

void Arena::Pool::Dump() const {
    printf("  pool '%s' slot size %zu, %zu pages committed:\n",
           mapping_->name(), slot_size_, mapping_->AllocatedPages());
    printf("  |     start 0x%p\n", start_);
    size_t nslots = static_cast<size_t>(top_ - start_) / slot_size_;
    printf("  |       top 0x%p (%zu slots popped)\n", top_, nslots);
    const size_t np = static_cast<size_t>(committed_ - start_) / PAGE_SIZE;
    const size_t npmax = static_cast<size_t>(committed_max_ - start_) / PAGE_SIZE;
    printf("  | committed 0x%p (%zu pages; %zu pages max)\n",
           committed_, np, npmax);
    nslots = static_cast<size_t>(end_ - start_) / slot_size_;
    printf("  \\       end 0x%p (%zu slots total)\n", end_, nslots);
}

void* Arena::Alloc() {
    DEBUG_ASSERT(vmar_ != nullptr);
    // Prefers to return the most-recently-freed slot in the hopes that
    // it is still hot in the cache.
    if (!free_.is_empty()) {
        // TODO(dbort): Replace this linked list with a stack of offsets into
        // the data pool; simpler, and uses less memory.
        Node* node = free_.pop_front();
        auto slot = node->slot;
        control_.Push(node);
        return slot;
    } else {
        return data_.Pop();
    }
}

void Arena::Free(void* addr) {
    DEBUG_ASSERT(vmar_ != nullptr);
    if (addr == nullptr) {
        return;
    }
    DEBUG_ASSERT(data_.InRange(addr));
    Node* node = new (reinterpret_cast<void*>(control_.Pop())) Node{addr};
    free_.push_front(node);
}

void Arena::Dump() const {
    DEBUG_ASSERT(vmar_ != nullptr);
    printf("%s mappings:\n", vmar_->name());
    // Not completely safe, since we don't hold the VMAR's aspace
    // lock, but we're the only ones who know about the mappings
    // and we're not modifying anything.
    vmar_->Dump(/* depth */ 1, /* verbose */ true);
    printf("%s pools:\n", vmar_->name());
    control_.Dump();
    data_.Dump();
    printf("%s free list: %zu nodes\n", vmar_->name(), free_.size_slow());
}

} // namespace mxtl
