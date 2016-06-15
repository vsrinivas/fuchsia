// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <utils/arena.h>

#include <assert.h>
#include <err.h>
#include <new.h>
#include <stdio.h>
#include <string.h>

#include <kernel/vm.h>

namespace utils {

Arena::Arena()
    : ob_size_(0u),
      c_start_(nullptr),
      c_top_(nullptr),
      d_start_(nullptr),
      d_top_(nullptr),
      d_end_(nullptr) {}

Arena::~Arena() {
    free_.clear();
    auto kspace = vmm_get_kernel_aspace();
    if (c_start_) vmm_free_region(kspace, reinterpret_cast<vaddr_t>(c_start_));
    if (d_start_) vmm_free_region(kspace, reinterpret_cast<vaddr_t>(d_start_));
}

status_t Arena::Init(const char* name, size_t ob_size, size_t count) {
    if (!ob_size) return ERR_INVALID_ARGS;
    if (!count) return ERR_INVALID_ARGS;

    ob_size_ = ob_size;

    char vname[24 + 8] = {};
    if (strlen(name) >= (sizeof(vname) - 8)) return ERR_INVALID_ARGS;

    auto kspace = vmm_get_kernel_aspace();
    void* start = nullptr;
    status_t st;

    sprintf(vname, "%s_ctrl", name);
    st = vmm_alloc(kspace, vname, count * sizeof(Node), &start, PAGE_SIZE_SHIFT, 0,
                   ARCH_MMU_FLAG_PERM_NO_EXECUTE);
    if (st < 0) return st;

    c_start_ = reinterpret_cast<char*>(start);
    c_top_ = c_start_;

    sprintf(vname, "%s_data", name);
    st = vmm_alloc(kspace, vname, count * ob_size, &start, PAGE_SIZE_SHIFT, 0,
                   ARCH_MMU_FLAG_PERM_NO_EXECUTE);
    if (st < 0) {
        vmm_free_region(kspace, reinterpret_cast<vaddr_t>(c_start_));
        return st;
    }

    d_start_ = reinterpret_cast<char*>(start);
    d_top_ = d_start_;

    d_end_ = d_start_ + count * ob_size;

    return NO_ERROR;
}

void* Arena::Alloc() {
    // Prefers to give a previously used memory in the hopes that it is
    // still in the cache.
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

    auto node = new (reinterpret_cast<void*>(c_top_)) Node{nullptr, addr};
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
}
