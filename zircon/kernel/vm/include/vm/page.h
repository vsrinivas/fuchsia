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

#include <fbl/atomic_ref.h>
#include <kernel/event.h>
#include <ktl/atomic.h>
#include <ktl/type_traits.h>
#include <vm/page_state.h>
#include <vm/page_queue.h>

// Used for waiting on a loaned page to exit any short duration temporary state in which the page is
// held locally by a thread performing an action on that page, and can't be returned to the loaning
// VmCowPages.  Actions include allocating + adding a page, moving a page from one VmCowPages to
// another, and remvoing + freeing a page.
struct StackLinkableEvent {
  // non-auto-reset
  Event event;
  StackLinkableEvent* next;
};

// core per page structure allocated at pmm arena creation time
struct vm_page {
  // protected by PageQueues lock
  struct list_node queue_node;

  // read-only after being set up
  paddr_t paddr_priv;  // use paddr() accessor

  // offset 0x18

  static constexpr uint64_t kObjectOrEventIsEvent = std::numeric_limits<uint64_t>::max();
  union {
    struct {
      // This field is used for two different purposes, depending on whether page_offset_priv is set
      // to kObjectOrEventIsEvent or not.
      //
      // When page_offset_priv is not set to kObjectOrEventIsEvent:
      //
      // This is an optional back pointer to the vm object this page is currently contained in.  It
      // is implicitly valid when the page is in a VmCowPages (which is a superset of intervals
      // during which the page is in a page queue), and nullptr (or logically nullptr) otherwise.
      // This should not be modified (except under the page queue lock) whilst a page is in a
      // VmCowPages.
      //
      // When page_offset_priv is set to kObjectOrEventIsEvent:
      //
      // During page states ALLOC, OBJECT when the page is not in any VmCowPages (such as during
      // page moves), or FREE, this field can point to the first item of a singly-linked list of
      // Event(s) to notify when the page either again is in a VmCowPages, or when the page has
      // become FREE.  This is used in chasing down and replacing loaned pages that are being
      // borrowed, so that the loaned page can be returned to its contiguous VmCowPages.
      //
      // This field is atomic to allow atomically removing the whole StackLinkableEvent list while
      // another thread is adding an item to the list.  While adding to the list is always under
      // PageQueues lock, removing the whole list can happen under PageQueues lock or the PmmNode
      // lock.  Removal from the event list is necesssary in rare cases such as thread interruption,
      // but removal isn't fast.
      //
      // It is possible for an event to be queued, then a check for FREE after adding the event to
      // miss the FREE state because the page has already moved on, and for the event to then be
      // triggered on subsequent transition to OBJECT when the page is added to the contiguous VMO,
      // added to a non-contiguous VMO (while still loaned), or when the contiguous VMO is deleted.
      // These cases all quickly notify the waiter, and the combination of these cases avoids
      // letting the page be put to some arbitrary non-VmCowPages use that wouldn't notify the
      // waiter.
      //
      // General info:
      //
      // Field should be modified by the setters and getters to allow for future encoding changes.
      //
      // Onwership of the object_or_event_priv field and page_offset_priv fields is with PageQueues
      // when the page is in ALLOC or OBJECT states, but is with PmmNode when the page is in FREE
      // state.
      ktl::atomic<void*> object_or_event_priv;

      // offset 0x20

      // When object_or_event_priv is pointing to a VmCowPages, this is the offset in the VmCowPages
      // that contains this page.
      //
      // When object_or_event_priv is pointing to an Event, this is kObjectOrEventIsEvent.
      //
      // Else this field is 0.
      //
      // Field should be modified by the setters and getters to allow for future encoding changes.
      //
      // Protected by PageQueues lock.  When the pmm notifies waiters upon FREE, it does not set
      // page_offset_priv from kObjectOrEventIsEvent back to 0.  That is done later next time
      // a VmCowPages adds the page to PageQueues.
      uint64_t page_offset_priv;

      // offset 0x28

      // Identifies which queue this page is in.
      //
      // Protected by PageQueues lock.
      uint8_t page_queue_priv;

      // offset 0x29

      // under PageQueues lock
      void* get_object() const {
        if (unlikely(page_offset_priv == kObjectOrEventIsEvent)) {
          return nullptr;
        }
        return object_or_event_priv;
      }

      // under PageQueues lock
      void set_object(void* obj) {
        if (unlikely(page_offset_priv == kObjectOrEventIsEvent && obj)) {
          // caller is presently excluding addition of more waiters
          signal_waiters();
          page_offset_priv = 0;
        }
        object_or_event_priv = obj;
      }

      // under PageQueues lock
      void add_event(StackLinkableEvent* event) {
        DEBUG_ASSERT(event);
        // PageQueues lock is preventing any other thread from adding concurrently, but removal of
        // the whole list concurrently is possible if the page becomes FREE instead of getting a
        // VmCowPages.
        while (true) {
          StackLinkableEvent* old_head = load_event();
          event->next = old_head;
          // The caller will be checking with pmm to see if page has become FREE after adding the
          // event.  We only need atomicity from this operation.  Everything else we need re.
          // synchronization, we get from the PageQueues lock and PmmNode lock.
          void* old_head_ptr = old_head;
          if (object_or_event_priv.compare_exchange_weak(old_head_ptr, reinterpret_cast<void*>(event), std::memory_order_relaxed)) {
            break;
          }
        }
        page_offset_priv = kObjectOrEventIsEvent;
      }

      // This process is not particularly fast if there are many waiters, and the caller is
      // responsible for re-checking with pmm for FREE state after removal.  Removal is rare.
      //
      // Because we're holding the PageQueues lock we know that no more events will be added or
      // removed during this method, but pmm may attempt to notify during this method, so to avoid
      // events being signaled and deleted while we're removing one event, we snap off the whole
      // list while we're removing one item, then put the rest of the list back.
      //
      // The pmm may snap off the whole list also, during signal_waiters, which can run
      // concurrently.  The remove may therefore not find the event.  When this occurs, the caller's
      // acquisition of the pmm lock (prior to the caller deleting the event) will fence out
      // signal_waiters, at which point it becomes safe for the caller to delete the event.
      //
      // under PageQueues lock
      void remove_event(StackLinkableEvent* event) {
        DEBUG_ASSERT(event);
        if (page_offset_priv != kObjectOrEventIsEvent) {
          // Already safe for caller to delete event.
          return;
        }
        // temporarily grab the whole list
        StackLinkableEvent* head = reinterpret_cast<StackLinkableEvent*>(object_or_event_priv.exchange(nullptr, std::memory_order_relaxed));
        // remove event, if present
        StackLinkableEvent** iter = &head;
        while (*iter) {
          if (*iter == event) {
            *iter = (*iter)->next;
            break;
          }
          iter = &(*iter)->next;
        }
        // put remainder of list back - caller will signal_waiters if page became FREE
        object_or_event_priv.exchange(head, std::memory_order_relaxed);
      }

      // under PageQueues lock
      uint64_t get_page_offset() const {
        if (unlikely(page_offset_priv == kObjectOrEventIsEvent)) {
          return 0;
        }
        return page_offset_priv;
      }

      // under PageQueues lock
      void set_page_offset(uint64_t page_offset) {
        DEBUG_ASSERT(page_offset_priv != kObjectOrEventIsEvent);
        page_offset_priv = page_offset;
      }

      fbl::atomic_ref<uint8_t> get_page_queue_ref() {
        return fbl::atomic_ref<uint8_t>(page_queue_priv);
      }
      fbl::atomic_ref<const uint8_t> get_page_queue_ref() const {
        return fbl::atomic_ref<const uint8_t>(page_queue_priv);
      }

      // This can be called under PageQueues lock, or under PmmNode lock.  When called under
      // PageQueues lock, the page is getting a new VmCowPages (either switching from ALLOC to
      // OBJECT, or setting a new object at the end of a move of the page between VmCowPages(s)).
      // When called under PmmNode lock, the page has recently become FREE and is currently still
      // FREE.
      //
      // The call to signal_waiters can race with remove_event, but only when signal_waiters is
      // called under PmmNode lock.  This race is dealt with by having the remove_event caller check
      // with pmm to see if the page is FREE after remove_event.
      //
      // Under PageQueues lock or PmmNode lock, depending on page state.
      void signal_waiters() {
        StackLinkableEvent* head = reinterpret_cast<StackLinkableEvent*>(object_or_event_priv.exchange(nullptr, std::memory_order_relaxed));
        for (StackLinkableEvent* event = head; event; event = event->next) {
          // We don't have to worry about deletion of an item while iterating the list, because
          // set_object and set_new_object_event are both protectecd by a PageQueues lock.
          event->event.Signal();
        }
      }

      // Under PmmNode lock, with page FREE.
      void signal_any_waiters() {
        if (unlikely(page_offset_priv == kObjectOrEventIsEvent)) {
          signal_waiters();
        }
      }

      // logically private
      StackLinkableEvent* load_event() {
        StackLinkableEvent* result = reinterpret_cast<StackLinkableEvent*>(object_or_event_priv.load(std::memory_order_relaxed));
        DEBUG_ASSERT(!result || page_offset_priv == kObjectOrEventIsEvent);
        return result;
      }

#define VM_PAGE_OBJECT_PIN_COUNT_BITS 5
#define VM_PAGE_OBJECT_MAX_PIN_COUNT ((1ul << VM_PAGE_OBJECT_PIN_COUNT_BITS) - 1)
      // Only modified under VmCowPages lock.
      //
      // Read under PageQueues lock, but only when page_queue_priv != PageQueueWired.
      //
      // PageQueues can safely read the pin_count_or_age_priv field as the unpin_time whenever
      // page_queue_priv != PageQueueWired, without holding the VmCowPages lock.
      //
      // The logical pin_count field is the number of times the page is pinned.  This logical
      // field is 0 when page_queue_priv != PageQueueWired.
      //
      // The logical unpin_time field is the monotonic timestamp, divided down, at which the page
      // last was un-pinned since allocated.  If the page hasn't been un-pinned since allocated,
      // the unpin_time is an arbitrary number, spread out in an attempt to avoid too many pages
      // simultaneously aliasing to appear to have been un-pinned recently even though they
      // weren't.  While page_queue_priv == PageQueueWired, the unpin_time logical field is not
      // read, and its value is undefined / unspecified / reading will DEBUG_ASSERT().
      uint8_t pin_count_or_unpin_time_priv : VM_PAGE_OBJECT_PIN_COUNT_BITS;

      uint8_t get_pin_count() const {
        if (page_queue_priv != PageQueue::PageQueueWired) {
          return 0;
        }
        uint8_t pin_count = pin_count_or_unpin_time_priv;
        return pin_count;
      }
      void set_pin_count(uint8_t pin_count) {
        DEBUG_ASSERT(page_queue_priv == PageQueue::PageQueueWired);
        DEBUG_ASSERT(pin_count <= VM_PAGE_OBJECT_MAX_PIN_COUNT);
        pin_count_or_unpin_time_priv = pin_count;
      }

#define VM_PAGE_OBJECT_UNPIN_TIME_BITS VM_PAGE_OBJECT_PIN_COUNT_BITS
#define VM_PAGE_OBJECT_UNPIN_TIME_MODULUS (1ul << VM_PAGE_OBJECT_UNPIN_TIME_BITS)
#define VM_PAGE_OBJECT_MAX_UNPIN_TIME ((1ul << VM_PAGE_OBJECT_UNPIN_TIME_BITS) - 1)
      uint32_t get_unpin_time() {
        DEBUG_ASSERT(page_queue_priv != PageQueue::PageQueueWired);
        uint8_t unpin_time = pin_count_or_unpin_time_priv;
        return unpin_time;
      }
      void set_unpin_time(uint32_t unpin_time) {
        DEBUG_ASSERT(page_queue_priv != PageQueue::PageQueueWired);
        DEBUG_ASSERT(unpin_time <= VM_PAGE_OBJECT_MAX_UNPIN_TIME);
        pin_count_or_unpin_time_priv = static_cast<uint8_t>(unpin_time);
      }

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
      //
      // under VmCowPages lock
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

  // If true, this page is "loaned" in the sense of being loaned from a contiguous VMO (via
  // decommit) to Zircon.  If the original contiguous VMO is deleted, this page will no longer be
  // loaned.  A loaned page cannot be pinned.  Instead a different physical page (non-loaned) is
  // used for the pin.  A loaned page can be (re-)committed back into its original contiguous VMO,
  // which causes the data in the loaned page to be moved into a different physical page (which
  // itself can be non-loaned or loaned).  A loaned page cannot be used to allocate a new contiguous
  // VMO.
  uint8_t loaned : 1;
  // If true, the original contiguous VMO wants the page back.  Such pages won't be re-used until
  // the page is no longer loaned, either via commit of the page back into the contiguous VMO that
  // loaned the page, or via deletion of the contiguous VMO that loaned the page.  Such pages are
  // not in the free_loaned_list_ in pmm, which is how re-use is prevented.
  uint8_t loan_cancelled : 1;

  // This padding is inserted here to make sizeof(vm_page) a multiple of 8 and help validate that
  // all commented offsets were indeed correct.
  uint8_t padding_bits : 6;
  char padding_bytes[4];

  // helper routines
  bool is_free() const { return state() == vm_page_state::FREE; }
  bool is_loaned() const { return loaned; }
  bool is_loan_cancelled() const { return loan_cancelled; }

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
