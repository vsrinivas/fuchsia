// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <mxtl/arena.h>

#include <assert.h>
#include <err.h>
#include <new.h>
#include <stdio.h>
#include <string.h>

#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>

namespace mxtl {

Arena::Arena()
    : ob_size_(0u),
      c_start_(nullptr),
      c_top_(nullptr),
      d_start_(nullptr),
      d_top_(nullptr),
      d_end_(nullptr),
      p_top_(nullptr) {
}

Arena::~Arena() {
    free_.clear();
    auto kspace = vmm_get_kernel_aspace();
    if (c_start_) vmm_free_region(kspace, reinterpret_cast<vaddr_t>(c_start_));
    if (d_start_) vmm_free_region(kspace, reinterpret_cast<vaddr_t>(d_start_));
}

status_t Arena::Init(const char* name, size_t ob_size, size_t count) {
    if ((ob_size == 0) || (ob_size > PAGE_SIZE))
        return ERR_INVALID_ARGS;
    if (!count)
        return ERR_INVALID_ARGS;

    ob_size_ = ob_size;

    char vname[24 + 8] = {};
    if (strlen(name) >= (sizeof(vname) - 8))
        return ERR_INVALID_ARGS;

    void* start = nullptr;
    status_t st;

    auto kspace = vmm_get_kernel_aspace();

    // Allocate the control zone, ddemand paged.
    sprintf(vname, "%s_ctrl", name);
    st = vmm_alloc(kspace, vname, count * sizeof(Node), &start, PAGE_SIZE_SHIFT,
                   0, 0, ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
    if (st < 0)
        return st;

    c_start_ = reinterpret_cast<char*>(start);
    c_top_ = c_start_;

    // Allocate the data zone, demand-paged (although we do manual range commits).
    auto data_mem_sz = count * ob_size;

    sprintf(vname, "%s_data", name);
    vmo_ = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, data_mem_sz);
    if (!vmo_) {
        vmm_free_region(kspace, reinterpret_cast<vaddr_t>(c_start_));
        return ERR_NO_MEMORY;
    }

    st = vmm_aspace_to_obj(kspace)->MapObject(
            vmo_, vname, 0u, data_mem_sz, &start, PAGE_SIZE_SHIFT,
            0, 0, ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);

    if (st < 0)
        return st;

    d_start_ = reinterpret_cast<char*>(start);
    d_top_ = d_start_;
    p_top_ = d_top_;

    d_end_ = d_start_ + count * ob_size;

    return NO_ERROR;
}

void* Arena::Alloc() {
    // Prefers to give a previously used memory in the hopes that it is
    // still in hot the cache.
    if (!free_.is_empty()) {
        auto node = free_.pop_front();
        c_top_ -= sizeof(Node);
        return node->slot;
    } else if (d_top_ < d_end_) {
        CommitMemoryAheadIfNeeded();
        auto slot = d_top_;
        d_top_ += ob_size_;
        return slot;
    } else {
        return nullptr;
    }
}

void Arena::Free(void* addr) {
    if (!addr) return;
    DEBUG_ASSERT((addr >= d_start_) && (addr < d_end_));

    auto node = new (reinterpret_cast<void*>(c_top_)) Node{addr};
    c_top_ += sizeof(Node);
    free_.push_front(node);
}

size_t Arena::Trim() {
    // TODO(cpu). Impement this. The general idea is:
    // 1 - find max |slot| in the |free_| list.
    // 2 - compute pages bettween max of #1 and |d_end_|
    // 3-  free pages, adjust d_end_, etc;
    return 0u;
}

void Arena::CommitMemoryAheadIfNeeded() {
   if ((p_top_ - d_top_) >= PAGE_SIZE)
        return;

    // The top of the used range is close to the edge of commited memory,
    // rather than suffer a page fault, we commit ahead 4 pages or less
    // if we are near the end.

    auto len = vmo_->CommitRange(p_top_ - d_start_, 4 * PAGE_SIZE);
    if (len < 0) {
        // it seems we ran out of physical memory.
        panic("failed to commit arena pages\n");
    }

    p_top_ += len;
}

}
