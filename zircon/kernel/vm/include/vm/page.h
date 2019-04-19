// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <list.h>
#include <stdint.h>
#include <sys/types.h>
#include <vm/page_state.h>
#include <zircon/compiler.h>

// core per page structure allocated at pmm arena creation time
typedef struct vm_page {
    struct list_node queue_node;
    paddr_t paddr_priv; // use paddr() accessor
    // offset 0x18

    struct {
        uint32_t flags : 8;
        // logically private; use |state()| and |set_state()|
        uint32_t state_priv : VM_PAGE_STATE_BITS;
    };
    // offset: 0x1c

    union {
        struct {
#define VM_PAGE_OBJECT_PIN_COUNT_BITS 5
#define VM_PAGE_OBJECT_MAX_PIN_COUNT ((1ul << VM_PAGE_OBJECT_PIN_COUNT_BITS) - 1)

            uint8_t pin_count : VM_PAGE_OBJECT_PIN_COUNT_BITS;
        } object; // attached to a vm object
    };

    // helper routines
    bool is_free() const {
        return state_priv == VM_PAGE_STATE_FREE;
    }

    void dump() const;

    // return the physical address
    // future plan to store in a compressed form
    paddr_t paddr() const { return paddr_priv; }

    vm_page_state state() const { return vm_page_state(state_priv); }

    void set_state(vm_page_state new_state);

    // Return the approximate number of pages in state |state|.
    //
    // When called concurrently with |set_state|, the count may be off by a small amount.
    static uint64_t get_count(vm_page_state state);

    // Add |n| to the count of pages in state |state|.
    //
    // Should be used when first constructing pages.
    static void add_to_initial_count(vm_page_state state, uint64_t n);

} vm_page_t;

// assert that the page structure isn't growing uncontrollably
static_assert(sizeof(vm_page) == 0x20, "");

// helpers
const char* page_state_to_string(unsigned int state);
