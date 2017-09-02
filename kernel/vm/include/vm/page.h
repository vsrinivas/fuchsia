// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <list.h>
#include <magenta/compiler.h>
#include <stdint.h>

// forward declare
class VmObject;

#define VM_PAGE_OBJECT_PIN_COUNT_BITS 5
#define VM_PAGE_OBJECT_MAX_PIN_COUNT ((1ul << VM_PAGE_OBJECT_PIN_COUNT_BITS) - 1)

// core per page structure
typedef struct vm_page {
    struct {
        uint32_t flags : 8;
        uint32_t state : 3;
    };
    uint32_t map_count;

    union {
        struct {
            // in allocated/just freed state, use a linked list to hold the page in a queue
            struct list_node node;
        } free;
        struct {
            // attached to a vm object
            uint64_t offset; // unused currently
            VmObject* obj;   // unused currently

            uint8_t pin_count : VM_PAGE_OBJECT_PIN_COUNT_BITS;
            // If true, one pin slot is used by the VmObject to keep a run
            // contiguous.
            bool contiguous_pin : 1;
        } object;

        uint8_t pad[24]; // pad out to 32 bytes
    };
} vm_page_t;

// pmm will maintain pages of this size
#define VM_PAGE_STRUCT_SIZE (sizeof(vm_page_t))
static_assert(sizeof(vm_page_t) == 32, "");

enum vm_page_state {
    VM_PAGE_STATE_FREE,
    VM_PAGE_STATE_ALLOC,
    VM_PAGE_STATE_WIRED,
    VM_PAGE_STATE_HEAP,
    VM_PAGE_STATE_OBJECT,
    VM_PAGE_STATE_MMU, /* allocated to serve arch-specific mmu purposes */

    _VM_PAGE_STATE_COUNT
};

// helpers
static inline bool page_is_free(const vm_page_t* page) {
    return page->state == VM_PAGE_STATE_FREE;
}

const char* page_state_to_string(unsigned int state);
void dump_page(const vm_page_t* page);
