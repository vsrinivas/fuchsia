// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <stdint.h>

// Defines the state of a VM page (|vm_page_t|).
//
// Be sure to keep this enum in sync with the definition of |vm_page_t|.
enum vm_page_state : uint32_t {
    VM_PAGE_STATE_FREE = 0,
    VM_PAGE_STATE_ALLOC,
    VM_PAGE_STATE_OBJECT,
    VM_PAGE_STATE_WIRED,
    VM_PAGE_STATE_HEAP,
    VM_PAGE_STATE_MMU,   // allocated to serve arch-specific mmu purposes
    VM_PAGE_STATE_IOMMU, // allocated for platform-specific iommu structures
    VM_PAGE_STATE_IPC,

    VM_PAGE_STATE_COUNT_
};

#define VM_PAGE_STATE_BITS 3
static_assert((1u << VM_PAGE_STATE_BITS) >= VM_PAGE_STATE_COUNT_, "");

typedef struct vm_page_counts {
    int64_t by_state[VM_PAGE_STATE_COUNT_];
} vm_page_counts_t;
