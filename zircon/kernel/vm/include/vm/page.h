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

#include <ktl/atomic.h>
#include <ktl/type_traits.h>
#include <vm/page_state.h>

// core per page structure allocated at pmm arena creation time
struct vm_page {
  struct list_node queue_node;
  paddr_t paddr_priv;  // use paddr() accessor

  // offset 0x18

  union {
    struct {
      // This is an optional back pointer to the vm object this page is currently contained in. It
      // is implicitly valid when the page is in the pager_backed page queue, and not valid
      // otherwise. Consequently, to prevent data races, this should not be modified (except under
      // the page queue lock) whilst a page is in a page queue.
      // Field should be modified by the setters and getters to allow for future encoding changes.
      void* object_priv;

      // offset 0x20

      // When object is valid, this is the offset in the vmo that contains this page.
      // Field should be modified by the setters and getters to allow for future encoding changes.
      uint64_t page_offset_priv;

      // offset 0x28

      // Identifies which queue this page is in.
      uint8_t page_queue_priv;

      // offset 0x29

      void* get_object() const { return object_priv; }
      void set_object(void* obj) { object_priv = obj; }
      uint64_t get_page_offset() const { return page_offset_priv; }
      void set_page_offset(uint64_t page_offset) { page_offset_priv = page_offset; }
      ktl::atomic_ref<uint8_t> get_page_queue_ref() {
        return ktl::atomic_ref<uint8_t>(page_queue_priv);
      }
      ktl::atomic_ref<const uint8_t> get_page_queue_ref() const {
        return ktl::atomic_ref<const uint8_t>(page_queue_priv);
      }

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
      // Hint for whether the page is always needed and should not be considered for reclamation
      // under memory pressure (unless the kernel decides to override hints for some reason).
      uint8_t always_need : 1;
      // This struct has no type name and exists inside an unpacked parent and so it really doesn't
      // need to have any padding. By making it packed we allow the next outer variables, to use
      // space we would have otherwise wasted in padding, without breaking alignment rules.
    } __PACKED object;  // attached to a vm object
  };

  // offset 0x2a

  // logically private; use |state()| and |set_state()|
  ktl::atomic<vm_page_state> state_priv;

  // offset 0x2b

  // This padding is inserted here to make sizeof(vm_page) a multiple of 8 and help validate that
  // all commented offsets were indeed correct.
  char padding[5];

  // helper routines
  bool is_free() const { return state() == vm_page_state::FREE; }

  void dump() const;

  // return the physical address
  // future plan to store in a compressed form
  paddr_t paddr() const { return paddr_priv; }

  vm_page_state state() const { return state_priv.load(ktl::memory_order_relaxed); }

  void set_state(vm_page_state new_state);

  // Return the approximate number of pages in state |state|.
  //
  // When called concurrently with |set_state|, the count may be off by a small amount.
  static uint64_t get_count(vm_page_state state);

  // Add |n| to the count of pages in state |state|.
  //
  // Should be used when first constructing pages.
  static void add_to_initial_count(vm_page_state state, uint64_t n);
};

// Provide a type alias using modern syntax to avoid clang-tidy warnings.
using vm_page_t = vm_page;

// assert that the page structure isn't growing uncontrollably
static_assert(sizeof(vm_page) == 0x30);

// assert that |vm_page| is a POD
static_assert(ktl::is_trivial_v<vm_page> && ktl::is_standard_layout_v<vm_page>);

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_H_
