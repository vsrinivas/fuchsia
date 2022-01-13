// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_H_

#include <lib/zircon-internal/macros.h>
#include <stdint.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/listnode.h>

#include <kernel/percpu.h>
#include <kernel/thread_lock.h>
#include <ktl/atomic.h>
#include <ktl/optional.h>
#include <ktl/type_traits.h>
#include <vm/page_state.h>
#include <vm/stack_owned_loaned_pages_interval.h>

// core per page structure allocated at pmm arena creation time
struct vm_page {
  struct list_node queue_node;

  // read-only after being set up
  paddr_t paddr_priv;  // use paddr() accessor

  // offset 0x18

  union {
    struct {
      // This field is used for two different purposes, depending on whether the low order bit is
      // set or not.  This same field exists in states OBJECT, ALLOC, and FREE.
      //
      // When all bits are 0:
      //
      // There is no object and no StackOwnedLoanedPagesInterval.
      //
      // When kObjectOrStackOwnerIsStackOwnerFlag is set:
      //
      // The rest of the bits are a pointer to a StackOwnedLoanedPagesInterval.  This allows a
      // thread reclaiming a loaned page to apply priority inheritance onto the thread whose stack
      // is transiently owning a loaned page.  The StackOwnedLoanedPagesInterval has an
      // OwnedWaitQueue that's used to avoid priority inversion while the reclaiming thread is
      // waiting for the loaned page to no longer be stack owned.  This brief waiting is part of
      // chasing down and replacing loaned pages that are being borrowed, so that the loaned page
      // can be returned to its contiguous VmCowPages.
      //
      // When kObjectOrStackOwnerIsStackOwnerFlag bit is 0 but any other bits are 1:
      //
      // This is a back pointer to the vm object this page is currently contained in.  It is
      // implicitly valid when the page is in a VmCowPages (which is a superset of intervals
      // during which the page is in a page queue), and nullptr (or logically nullptr) otherwise.
      // This should not be modified (except under the page queue lock) whilst a page is in a
      // VmCowPages.
      //
      // More details:
      //
      // This field is accessed via an atomic_ref<>.  Using ktl::atomic<> here seems to make GCC
      // unhappy, depite the offset and alignment being fine (verified by static asserts and
      // DEBUG_ASSERT()s).  So instead we use atomic_ref<>.
      //
      // If a page is loaned, installation of StackOwnedLoanedPagesInterval on a page must occur
      // before any stack ownership of the page has begun, and removal of
      // StackOwnedLoanedPagesInterval must occur after stack ownership of the page has already
      // ended.
      //
      // This field is a struct to enforce that all access is at least atomic memory_order_relaxed.
      //
      // Field should be modified by the setters and getters to allow for future encoding changes.
      //
      // Any changes to this field need to be made to "alloc" and "free" union variants also.
      struct {
       public:
        ktl::atomic_ref<uintptr_t> get() {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
          // This is fine, because vm_page_t are 8 byte aligned, and object_or_stack_owner_priv is 8
          // byte aligned within the vm_page_t.
          static_assert(offsetof(vm_page, object.object_or_stack_owner.object_or_stack_owner) %
                            sizeof(decltype(object_or_stack_owner)) ==
                        0);
          DEBUG_ASSERT(reinterpret_cast<uintptr_t>(&object_or_stack_owner) %
                           sizeof(decltype(object_or_stack_owner)) ==
                       0);
          return ktl::atomic_ref<uintptr_t>(*&object_or_stack_owner);
#pragma GCC diagnostic pop
        }

        ktl::atomic_ref<const uintptr_t> get() const {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
          // This is fine, because vm_page_t are 8 byte aligned, and object_or_stack_owner_priv is 8
          // byte aligned within the vm_page_t.
          static_assert(offsetof(vm_page, object.object_or_stack_owner.object_or_stack_owner) %
                            sizeof(decltype(object_or_stack_owner)) ==
                        0);
          DEBUG_ASSERT(reinterpret_cast<uintptr_t>(&object_or_stack_owner) %
                           sizeof(decltype(object_or_stack_owner)) ==
                       0);
          return ktl::atomic_ref<const uintptr_t>(*&object_or_stack_owner);
#pragma GCC diagnostic pop
        }

       private:
        uintptr_t object_or_stack_owner;

       public:
        // Only for a static_assert() below.  Logically private.
        using InternalType = decltype(object_or_stack_owner);
      } __PACKED object_or_stack_owner;
      using object_or_stack_owner_t = decltype(object_or_stack_owner);

      void* get_object() const {
        uintptr_t value = object_or_stack_owner.get().load(ktl::memory_order_relaxed);
        if (unlikely(value & kObjectOrStackOwnerIsStackOwnerFlag)) {
          return nullptr;
        }
        return reinterpret_cast<void*>(value);
      }

      // offset 0x20

      // This also logically does clear_stack_owner() atomically.
      void set_object(void* obj) {
        // If the caller wants to clear the object, use clear_object() instead.
        DEBUG_ASSERT(obj);
        // Calling set_object() on a loaned page requires a StackOwnedLoanedPagesInterval on the
        // current stack.  If the object is already set, the stack ownership interval is essentially
        // quite short and all under a single VmCowPages hierarchy lock hold interval.  But we still
        // require the StackOwnedLoanedPagesInterval for consistency, since the page can be moving
        // between different VmCowPages, so in a sense it still stack owned.
        //
        // For longer stack ownership intervals (those not entirely under a single VmCowPages
        // hieararchy lock hold interval), the object won't be set on entry to this method, and we
        // can verify that a StackOwnedLoanedPagesInterval was set on the page, and is still the
        // current active interval.
#if DEBUG_ASSERT_IMPLEMENTED
        if (containerof(this, vm_page, object)->is_loaned()) {
          Thread* current_thread = Thread::Current::Get();
          if (!get_object()) {
            DEBUG_ASSERT(is_stack_owned());
            DEBUG_ASSERT(current_thread->stack_owned_loaned_pages_interval() == &stack_owner());
          } else if (obj != get_object()) {
            DEBUG_ASSERT(current_thread->stack_owned_loaned_pages_interval());
          }
        }
#endif
        // Ensure set_object() is visible after set_page_offset().
        ktl::atomic_thread_fence(ktl::memory_order_release);
        if (is_stack_owned()) {
          clear_stack_owner_internal(obj);
          return;
        }
        object_or_stack_owner.get().store(reinterpret_cast<uintptr_t>(obj),
                                          ktl::memory_order_relaxed);
      }

      // In addition to clearing object, this does set_stack_owner() atomically, if needed.
      void clear_object() {
        DEBUG_ASSERT(!is_stack_owned());
        if (containerof(this, vm_page, object)->is_loaned()) {
          Thread* current_thread = Thread::Current::Get();
          // To clear the object backlink of a loaned page, a StackOwnedLoanedPagesInterval on the
          // current stack is required.
          DEBUG_ASSERT(current_thread->stack_owned_loaned_pages_interval());
          set_stack_owner(current_thread->stack_owned_loaned_pages_interval());
          return;
        }
        object_or_stack_owner.get().store(0, ktl::memory_order_relaxed);
      }

      StackOwnedLoanedPagesInterval* maybe_stack_owner() const {
        uintptr_t value = object_or_stack_owner.get().load(ktl::memory_order_relaxed);
        if (!(value & kObjectOrStackOwnerIsStackOwnerFlag)) {
          return nullptr;
        }
        return reinterpret_cast<StackOwnedLoanedPagesInterval*>(value & ~kObjectOrStackOwnerFlags);
      }

      StackOwnedLoanedPagesInterval& stack_owner() const {
        uintptr_t value = object_or_stack_owner.get().load(ktl::memory_order_relaxed);
        DEBUG_ASSERT(value & kObjectOrStackOwnerIsStackOwnerFlag);
        return *reinterpret_cast<StackOwnedLoanedPagesInterval*>(value & ~kObjectOrStackOwnerFlags);
      }

      void set_stack_owner(StackOwnedLoanedPagesInterval* stack_owner) {
        DEBUG_ASSERT(stack_owner);
        // The stack owner shouldn't be set by the caller in situations where the/a stack owner is
        // already set.  It is expected that the field may currently be set to a VmCowPages*, but
        // that won't have the kObjectOrStackOwnerIsStackOwnerFlag bit set due to pointer alignment.
        DEBUG_ASSERT(!(object_or_stack_owner.get().load(ktl::memory_order_relaxed) &
                       kObjectOrStackOwnerIsStackOwnerFlag));
        // We use relaxed here because we're only relying on atomicity.  For ordering, the PmmNode
        // lock and PageQueues locks are relevant.  For ordering of a thread joining the owned wait
        // queue vs. deletion of the owned wait queue, the thread lock is relevant.
        object_or_stack_owner.get().store(
            reinterpret_cast<uintptr_t>(stack_owner) | kObjectOrStackOwnerIsStackOwnerFlag,
            ktl::memory_order_relaxed);
      }

      void clear_stack_owner() { clear_stack_owner_internal(nullptr); }

      void clear_stack_owner_internal(void* new_obj) {
        // If this fires, it likely means there's an extra clear somewhere, possibly by the current
        // thread, or possibly by a different thread.  This call could be the "extra" clear if the
        // caller didn't check whether there's a stack owner before calling.
        DEBUG_ASSERT(is_stack_owned());
        while (true) {
          uintptr_t old_value = object_or_stack_owner.get().load(ktl::memory_order_relaxed);
          // If this fires, it likely means that some other thread did a clear (so either this
          // thread or the other thread shouldn't have cleared).  If this thread had already done a
          // previous clear, the assert near the top would have fired instead.
          DEBUG_ASSERT(old_value & kObjectOrStackOwnerIsStackOwnerFlag);
          // We don't want to be acquiring thread_lock here every time we free a loaned page, so we
          // only acquire the thread_lock if the page's StackOwnedLoanedPagesInterval has a waiter,
          // which is much more rare.  In that case we must acquire the thread_lock to avoid letting
          // this thread continue and signal and delete the StackOwnedLoanedPagesInterval until
          // after the waiter has finished blocking on the OwnedWaitQueue, so that the waiter can be
          // woken and removed from the OwnedWaitQueue before the OwnedWaitQueue is deleted.
          ktl::optional<Guard<MonitoredSpinLock, IrqSave>> maybe_thread_lock_guard;
          if (old_value & kObjectOrStackOwnerHasWaiter) {
            // Acquire thread_lock.
            maybe_thread_lock_guard.emplace(ThreadLock::Get(), SOURCE_TAG);
          }
          if (object_or_stack_owner.get().compare_exchange_weak(
                  old_value, reinterpret_cast<uintptr_t>(new_obj), ktl::memory_order_relaxed)) {
            break;
          }
          // ~maybe_thread_lock_guard will release thread_lock if it was acquired
        }
      }

      bool is_stack_owned() const {
        // This can return true for a page that was loaned fairly recently but is no longer loaned.
        return !!(object_or_stack_owner.get().load(ktl::memory_order_relaxed) &
                  kObjectOrStackOwnerIsStackOwnerFlag);
      }

      struct TrySetHasWaiterResult {
        // True iff this call to try_set_has_waiter() was the first thread to set that there's a
        // waiter.
        bool first_setter;
        // The stack_owner may own the page.  The stack_owner can be waited on safely now that the
        // waiter bit is set.  The wait on stack_owner must occur while still the calling thread is
        // still holding the thread_lock.
        StackOwnedLoanedPagesInterval* stack_owner;
      };
      // ktl::is_ok() iff the page has a stack_owner and the waiter bit is set.
      // !ktl::is_ok() iff the page no longer has a stack_owner.
      ktl::optional<TrySetHasWaiterResult> try_set_has_waiter() TA_REQ(thread_lock);

      // offset 0x20

      // When object_or_event_priv is pointing to a VmCowPages, this is the offset in the VmCowPages
      // that contains this page.
      //
      // Else this field is 0.
      //
      // Field should be modified by the setters and getters to allow for future encoding changes.
      uint64_t page_offset_priv;

      uint64_t get_page_offset() const { return page_offset_priv; }

      void set_page_offset(uint64_t page_offset) { page_offset_priv = page_offset; }

      // offset 0x28

      // Identifies which queue this page is in.
      uint8_t page_queue_priv;

      ktl::atomic_ref<uint8_t> get_page_queue_ref() {
        return ktl::atomic_ref<uint8_t>(page_queue_priv);
      }
      ktl::atomic_ref<const uint8_t> get_page_queue_ref() const {
        return ktl::atomic_ref<const uint8_t>(page_queue_priv);
      }

      // offset 0x29

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

#define VM_PAGE_OBJECT_DIRTY_STATE_BITS 2
#define VM_PAGE_OBJECT_MAX_DIRTY_STATES ((1u << VM_PAGE_OBJECT_DIRTY_STATE_BITS))
      // Tracks state used to determine whether the page is dirty and its contents need to written
      // back to the page source at some point, and when it has been cleaned. Used for pages backed
      // by a user pager. The three states supported are Clean, Dirty, and AwaitingClean (more
      // details in VmCowPages::DirtyState).
      uint8_t dirty_state : VM_PAGE_OBJECT_DIRTY_STATE_BITS;

      uint8_t padding : 6;
      // This struct has no type name and exists inside an unpacked parent and so it really doesn't
      // need to have any padding. By making it packed we allow the next outer variables, to use
      // space we would have otherwise wasted in padding, without breaking alignment rules.
    } __PACKED object;  // attached to a vm object
    struct {
      // No fields may be added for these variants due to UB until we improve the stack ownership
      // system, or otherwise address the current usage of object.object_or_stack_owner outside
      // of OBJECT state.
    } free;  // free - typically in free_list_ or free_loaned_list_, unless loan_cancelled
    struct {
      // No fields may be added for these variants due to UB until we improve the stack ownership
      // system, or otherwise address the current usage of object.object_or_stack_owner outside
      // of OBJECT state.
    } alloc;  // allocated, but not yet put to any specific use
  };
  using object_t = decltype(object);

  // offset 0x2b

  // logically private; use |state()| and |set_state()|
  vm_page_state state_priv;

  // offset 0x2c

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
  char padding_bytes[3];

  // helper routines
  bool is_free() const { return state() == vm_page_state::FREE; }
  // TODO(dustingreen): Make is_loaned() atomically-readable so we can avoid pmm_is_loaned().
  bool is_loaned() const { return loaned; }
  bool is_loan_cancelled() const { return loan_cancelled; }

  void dump() const;

  // return the physical address
  // future plan to store in a compressed form
  paddr_t paddr() const { return paddr_priv; }

  vm_page_state state() const {
    return ktl::atomic_ref<const vm_page_state>(state_priv).load(ktl::memory_order_relaxed);
  }

  void set_state(vm_page_state new_state) {
    const vm_page_state old_state = state();
    ktl::atomic_ref<vm_page_state>(state_priv).store(new_state, ktl::memory_order_relaxed);

    // By only modifying the counters for the current CPU with preemption disabled, we can ensure
    // the values are not modified concurrently. See comment at the definition of |vm_page_counts|.
    percpu::WithCurrentPreemptDisable([&old_state, &new_state](percpu* p) {
      // Be sure to not block, else we lose the protection provided by disabling preemption.
      p->vm_page_counts.by_state[VmPageStateIndex(old_state)] -= 1;
      p->vm_page_counts.by_state[VmPageStateIndex(new_state)] += 1;
    });
  }

  // Return the approximate number of pages in state |state|.
  //
  // When called concurrently with |set_state|, the count may be off by a small amount.
  static uint64_t get_count(vm_page_state state);

  // Add |n| to the count of pages in state |state|.
  //
  // Should be used when first constructing pages.
  static void add_to_initial_count(vm_page_state state, uint64_t n);

 private:
  static constexpr uintptr_t kObjectOrStackOwnerIsStackOwnerFlag = 0x1;
  static constexpr uintptr_t kObjectOrStackOwnerHasWaiter = 0x2;
  static constexpr uintptr_t kObjectOrStackOwnerFlags = 0x3;
  // Make sure the address of a StackOwnedLoanedPagesInterval will always have room for at least 2
  // low order bit flags.
  static_assert(alignof(StackOwnedLoanedPagesInterval) >= kObjectOrStackOwnerFlags + 1);
};

// Provide a type alias using modern syntax to avoid clang-tidy warnings.
using vm_page_t = vm_page;

// assert expected offsets (the offsets in comments above) and natural alignments
static_assert(offsetof(vm_page_t, queue_node) == 0x0);
static_assert(offsetof(vm_page_t, queue_node) % alignof(decltype(vm_page_t::queue_node)) == 0);
static_assert(offsetof(vm_page_t, queue_node) % alignof(list_node) == 0);

static_assert(offsetof(vm_page_t, paddr_priv) == 0x10);
static_assert(offsetof(vm_page_t, paddr_priv) % alignof(decltype(vm_page_t::paddr_priv)) == 0);
static_assert(offsetof(vm_page_t, paddr_priv) % alignof(paddr_t) == 0);

static_assert(offsetof(vm_page_t, object.object_or_stack_owner) == 0x18);
static_assert(offsetof(vm_page_t, object.object_or_stack_owner) %
                  alignof(vm_page_t::object_t::object_or_stack_owner_t::InternalType) ==
              0);
static_assert(offsetof(vm_page_t, object.object_or_stack_owner) % alignof(uintptr_t) == 0);

static_assert(offsetof(vm_page_t, object.page_offset_priv) == 0x20);
static_assert(offsetof(vm_page_t, object.page_offset_priv) %
                  alignof(decltype(vm_page_t::object_t::page_offset_priv)) ==
              0);
static_assert(offsetof(vm_page_t, object.page_offset_priv) % alignof(uint64_t) == 0);

static_assert(offsetof(vm_page_t, object.page_queue_priv) == 0x28);
static_assert(offsetof(vm_page_t, object.page_queue_priv) %
                  alignof(decltype(vm_page_t::object_t::page_queue_priv)) ==
              0);
static_assert(offsetof(vm_page_t, object.page_queue_priv) % alignof(uint8_t) == 0);

static_assert(offsetof(vm_page_t, state_priv) == 0x2b);
static_assert(offsetof(vm_page_t, state_priv) % alignof(decltype(vm_page_t::state_priv)) == 0);
static_assert(offsetof(vm_page_t, state_priv) % alignof(vm_page_state) == 0);

static_assert(offsetof(vm_page_t, padding_bytes) == 0x2d);

// assert that the page structure isn't growing uncontrollably
static_assert(sizeof(vm_page) == 0x30);

// assert that |vm_page| is a POD
static_assert(ktl::is_trivial_v<vm_page> && ktl::is_standard_layout_v<vm_page>);

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_H_
