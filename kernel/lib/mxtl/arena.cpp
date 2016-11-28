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
#include <trace.h>

#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>

#define LOCAL_TRACE 0

namespace mxtl {

Arena::Arena()
    : ob_size_(0u),
      c_start_(nullptr),
      c_top_(nullptr),
      d_start_(nullptr),
      d_top_(nullptr),
      d_end_(nullptr) {
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

    LTRACEF("Arena name %s, ob_size %zu, count %zu\n", name, ob_size, count);

    ob_size_ = ob_size;

    char vname[24 + 8] = {};
    if (strlen(name) >= (sizeof(vname) - 8))
        return ERR_INVALID_ARGS;

    void* start = nullptr;
    status_t st;

    auto kspace = VmAspace::kernel_aspace();

    // Allocate the control zone
    const size_t control_mem_sz = count * sizeof(Node);

    control_vmo_ = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, control_mem_sz);
    if (!control_vmo_)
        return ERR_NO_MEMORY;

    // commit the entire object first
    uint64_t committed;
    st = control_vmo_->CommitRange(0, control_mem_sz, &committed);
    if (st < 0 || committed != control_mem_sz) {
        return ERR_NO_MEMORY;
    }

    // map it
    sprintf(vname, "%s_ctrl", name);
    st = kspace->MapObject(control_vmo_, vname, 0, control_mem_sz, &start,
                           PAGE_SIZE_SHIFT, 0, 0,
                           ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
    if (st < 0)
        return st;

    LTRACEF("control zone at %p, size %zu\n", start, control_mem_sz);

    c_start_ = reinterpret_cast<char*>(start);
    c_top_ = c_start_;

    // Allocate the data zone
    auto data_mem_sz = count * ob_size;

    sprintf(vname, "%s_data", name);
    vmo_ = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, data_mem_sz);
    if (!vmo_) {
        kspace->FreeRegion(reinterpret_cast<vaddr_t>(c_start_));
        return ERR_NO_MEMORY;
    }

    // commit the entire object first
    st = vmo_->CommitRange(0, data_mem_sz, &committed);
    if (st < 0 || committed != data_mem_sz) {
        kspace->FreeRegion(reinterpret_cast<vaddr_t>(c_start_));
        return ERR_NO_MEMORY;
    }

    // map it
    st = kspace->MapObject(
            vmo_, vname, 0u, data_mem_sz, &start, PAGE_SIZE_SHIFT,
            0, 0, ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);

    if (st < 0) {
        kspace->FreeRegion(reinterpret_cast<vaddr_t>(c_start_));
        return st;
    }

    LTRACEF("data zone at %p, size %zu\n", start, data_mem_sz);

    d_start_ = reinterpret_cast<char*>(start);
    d_top_ = d_start_;

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

}
