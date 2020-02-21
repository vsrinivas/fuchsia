// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_H_

#include <stdint.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/listnode.h>

#include <vm/page_state.h>

// core per page structure allocated at pmm arena creation time
typedef struct vm_page {
  struct list_node queue_node;
  paddr_t paddr_priv;  // use paddr() accessor
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

      // Bits used by VmObjectPaged implementation of COW clones.
      //
      // Pages of VmObjectPaged have two "split" bits. These bits are used to track which
      // pages in children of hidden VMOs have diverged from their parent. There are two
      // bits, left and right, one for each child. In a hidden parent, a 1 split bit means
      // that page in the child has diverged from the parent and the parent's page is
      // no longer accessible to that child.
      //
      // It should never be the case that both split bits are set, as the page should
      // be moved into the child instead of setting the second bit.
      uint8_t cow_left_split : 1;
      uint8_t cow_right_split : 1;
    } object;  // attached to a vm object
  };

  // helper routines
  bool is_free() const { return state_priv == VM_PAGE_STATE_FREE; }

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

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_H_
