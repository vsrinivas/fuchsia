// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "pmm_node.h"

#include <align.h>
#include <inttypes.h>
#include <lib/counters.h>
#include <lib/instrumentation/asan.h>
#include <lib/zircon-internal/macros.h>
#include <trace.h>

#include <new>

#include <fbl/algorithm.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/mp.h>
#include <kernel/thread.h>
#include <pretty/cpp/sizes.h>
#include <vm/bootalloc.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/pmm_checker.h>
#include <vm/stack_owned_loaned_pages_interval.h>

#include "vm/pmm.h"
#include "vm_priv.h"

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

using pretty::FormattedBytes;

// The number of PMM allocation calls that have failed.
KCOUNTER(pmm_alloc_failed, "vm.pmm.alloc.failed")

namespace {

void noop_callback(void* context, uint8_t idx) {}

// Indicates whether a PMM alloc call has ever failed with ZX_ERR_NO_MEMORY.  Used to trigger an OOM
// response.  See |MemoryWatchdog::WorkerThread|.
ktl::atomic<bool> alloc_failed_no_mem;

}  // namespace

// Poison a page |p| with value |value|. Accesses to a poisoned page via the physmap are not
// allowed and may cause faults or kASAN checks.
void PmmNode::AsanPoisonPage(vm_page_t* p, uint8_t value) {
#if __has_feature(address_sanitizer)
  asan_poison_shadow(reinterpret_cast<uintptr_t>(paddr_to_physmap(p->paddr())), PAGE_SIZE, value);
#endif  // __has_feature(address_sanitizer)
}

// Unpoison a page |p|. Accesses to a unpoisoned pages will not cause KASAN check failures.
void PmmNode::AsanUnpoisonPage(vm_page_t* p) {
#if __has_feature(address_sanitizer)
  asan_unpoison_shadow(reinterpret_cast<uintptr_t>(paddr_to_physmap(p->paddr())), PAGE_SIZE);
#endif  // __has_feature(address_sanitizer)
}

PmmNode::PmmNode() : evictor_(this) {
  // Initialize the reclamation watermarks such that system never
  // falls into a low memory state.
  uint64_t default_watermark = 0;
  InitReclamation(&default_watermark, 1, 0, nullptr, noop_callback);
}

PmmNode::~PmmNode() {}

// We disable thread safety analysis here, since this function is only called
// during early boot before threading exists.
zx_status_t PmmNode::AddArena(const pmm_arena_info_t* info) TA_NO_THREAD_SAFETY_ANALYSIS {
  dprintf(INFO, "PMM: adding arena %p name '%s' base %#" PRIxPTR " size %#zx\n", info, info->name,
          info->base, info->size);

  // Make sure we're in early boot (ints disabled and no active CPUs according
  // to the scheduler).
  DEBUG_ASSERT(mp_get_active_mask() == 0);
  DEBUG_ASSERT(arch_ints_disabled());

  DEBUG_ASSERT(IS_PAGE_ALIGNED(info->base));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(info->size));
  DEBUG_ASSERT(info->size > 0);

  // allocate a c++ arena object
  PmmArena* arena = new (boot_alloc_mem(sizeof(PmmArena))) PmmArena();

  // initialize the object
  auto status = arena->Init(info, this);
  if (status != ZX_OK) {
    // leaks boot allocator memory
    arena->~PmmArena();
    printf("PMM: pmm_add_arena failed to initialize arena\n");
    return status;
  }

  // walk the arena list, inserting in ascending order of arena base address
  for (auto& a : arena_list_) {
    if (a.base() > arena->base()) {
      arena_list_.insert(a, arena);
      goto done_add;
    }
  }

  // walked off the end, add it to the end of the list
  arena_list_.push_back(arena);

done_add:
  arena_cumulative_size_ += info->size;

  return ZX_OK;
}

size_t PmmNode::NumArenas() const {
  Guard<Mutex> guard{&lock_};
  return arena_list_.size();
}

zx_status_t PmmNode::GetArenaInfo(size_t count, uint64_t i, pmm_arena_info_t* buffer,
                                  size_t buffer_size) {
  Guard<Mutex> guard{&lock_};

  if ((count == 0) || (count + i > arena_list_.size()) || (i >= arena_list_.size())) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  const size_t size_required = count * sizeof(pmm_arena_info_t);
  if (buffer_size < size_required) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  // Skip the first |i| elements.
  auto iter = arena_list_.begin();
  for (uint64_t j = 0; j < i; j++) {
    iter++;
  }

  // Copy the next |count| elements.
  for (uint64_t j = 0; j < count; j++) {
    buffer[j] = iter->info();
    iter++;
  }

  return ZX_OK;
}

// called at boot time as arenas are brought online, no locks are acquired
void PmmNode::AddFreePages(list_node* list) TA_NO_THREAD_SAFETY_ANALYSIS {
  LTRACEF("list %p\n", list);

  uint64_t free_count = 0;
  vm_page *temp, *page;
  list_for_every_entry_safe (list, page, temp, vm_page, queue_node) {
    list_delete(&page->queue_node);
    DEBUG_ASSERT(!page->loaned);
    DEBUG_ASSERT(!page->loan_cancelled);
    DEBUG_ASSERT(page->is_free());
    list_add_tail(&free_list_, &page->queue_node);
    ++free_count;
  }
  free_count_.fetch_add(free_count);
  ASSERT(free_count_);
  free_pages_evt_.Signal();

  LTRACEF("free count now %" PRIu64 "\n", free_count_.load(ktl::memory_order_relaxed));
}

void PmmNode::FillFreePagesAndArm() {
  Guard<Mutex> guard{&lock_};

  if (!free_fill_enabled_) {
    return;
  }

  vm_page* page;
  list_for_every_entry (&free_list_, page, vm_page, queue_node) { checker_.FillPattern(page); }
  list_for_every_entry (&free_loaned_list_, page, vm_page, queue_node) {
    checker_.FillPattern(page);
  }

  // Now that every page has been filled, we can arm the checker.
  checker_.Arm();

  checker_.PrintStatus(stdout);
}

void PmmNode::CheckAllFreePages() {
  Guard<Mutex> guard{&lock_};

  if (!checker_.IsArmed()) {
    return;
  }

  uint64_t free_page_count = 0;
  uint64_t free_loaned_page_count = 0;
  vm_page* page;
  list_for_every_entry (&free_list_, page, vm_page, queue_node) {
    checker_.AssertPattern(page);
    ++free_page_count;
  }
  list_for_every_entry (&free_loaned_list_, page, vm_page, queue_node) {
    checker_.AssertPattern(page);
    ++free_loaned_page_count;
  }

  ASSERT(free_page_count == free_count_.load(ktl::memory_order_relaxed));
  ASSERT(free_loaned_page_count == free_loaned_count_.load(ktl::memory_order_relaxed));
}

#if __has_feature(address_sanitizer)
void PmmNode::PoisonAllFreePages() {
  Guard<Mutex> guard{&lock_};

  vm_page* page;
  list_for_every_entry (&free_list_, page, vm_page, queue_node) {
    AsanPoisonPage(page, kAsanPmmFreeMagic);
  };
  list_for_every_entry (&free_loaned_list_, page, vm_page, queue_node) {
    AsanPoisonPage(page, kAsanPmmFreeMagic);
  };
}
#endif  // __has_feature(address_sanitizer)

void PmmNode::EnableFreePageFilling(size_t fill_size, PmmChecker::Action action) {
  Guard<Mutex> guard{&lock_};
  checker_.SetFillSize(fill_size);
  checker_.SetAction(action);
  free_fill_enabled_ = true;
}

void PmmNode::DisableChecker() {
  Guard<Mutex> guard{&lock_};
  checker_.Disarm();
  free_fill_enabled_ = false;
}

void PmmNode::AllocPageHelperLocked(vm_page_t* page) {
  LTRACEF("allocating page %p, pa %#" PRIxPTR ", prev state %s\n", page, page->paddr(),
          page_state_to_string(page->state()));

  AsanUnpoisonPage(page);

  DEBUG_ASSERT(page->is_free());
  DEBUG_ASSERT(!page->object.is_stack_owned());

  if (page->is_loaned()) {
    page->object.set_stack_owner(&StackOwnedLoanedPagesInterval::current());
    // We want the set_stack_owner() to be visible before set_state(), but we don't need to make
    // set_state() a release just for the benefit of loaned pages, so we use this fence.
    ktl::atomic_thread_fence(ktl::memory_order_release);
  }

  page->set_state(vm_page_state::ALLOC);

  if (unlikely(free_fill_enabled_)) {
    checker_.AssertPattern(page);
  }
}

zx_status_t PmmNode::AllocPage(uint alloc_flags, vm_page_t** page_out, paddr_t* pa_out) {
  DEBUG_ASSERT(Thread::Current::memory_allocation_state().IsEnabled());
  AutoPreemptDisabler preempt_disable;
  Guard<Mutex> guard{&lock_};

  if (unlikely(InOomStateLocked())) {
    if (alloc_flags & PMM_ALLOC_DELAY_OK) {
      // TODO(stevensd): Differentiate 'cannot allocate now' from 'can never allocate'
      return ZX_ERR_NO_MEMORY;
    }
  }

  // If the caller sets PMM_ALLOC_FLAG_MUST_BORROW, the caller must also set
  // PMM_ALLOC_FLAG_CAN_BORROW.
  DEBUG_ASSERT(
      !((alloc_flags & PMM_ALLOC_FLAG_MUST_BORROW) && !(alloc_flags & PMM_ALLOC_FLAG_CAN_BORROW)));
  const bool can_borrow = pmm_physical_page_borrowing_config()->is_any_borrowing_enabled() &&
                          !!(alloc_flags & PMM_ALLOC_FLAG_CAN_BORROW);
  const bool must_borrow = can_borrow && !!(alloc_flags & PMM_ALLOC_FLAG_MUST_BORROW);
  const bool use_loaned_list = can_borrow && (!list_is_empty(&free_loaned_list_) || must_borrow);
  list_node* const which_list = use_loaned_list ? &free_loaned_list_ : &free_list_;

  vm_page* page = list_remove_head_type(which_list, vm_page, queue_node);
  if (!page) {
    if (!must_borrow) {
      // Allocation failures from the regular free list are likely to become user-visible.
      ReportAllocFailure();
    }
    return ZX_ERR_NO_MEMORY;
  }

  DEBUG_ASSERT(can_borrow || !page->is_loaned());
  AllocPageHelperLocked(page);

  if (use_loaned_list) {
    DecrementFreeLoanedCountLocked(1);
  } else {
    DecrementFreeCountLocked(1);
  }

  if (pa_out) {
    *pa_out = page->paddr();
  }

  if (page_out) {
    *page_out = page;
  }

  return ZX_OK;
}

zx_status_t PmmNode::AllocPages(size_t count, uint alloc_flags, list_node* list) {
  LTRACEF("count %zu\n", count);

  DEBUG_ASSERT(Thread::Current::memory_allocation_state().IsEnabled());
  // list must be initialized prior to calling this
  DEBUG_ASSERT(list);

  if (unlikely(count == 0)) {
    return ZX_OK;
  } else if (count == 1) {
    vm_page* page;
    zx_status_t status = AllocPage(alloc_flags, &page, nullptr);
    if (likely(status == ZX_OK)) {
      list_add_tail(list, &page->queue_node);
    }
    return status;
  }

  DEBUG_ASSERT(
      !((alloc_flags & PMM_ALLOC_FLAG_MUST_BORROW) && !(alloc_flags & PMM_ALLOC_FLAG_CAN_BORROW)));
  const bool can_borrow = pmm_physical_page_borrowing_config()->is_any_borrowing_enabled() &&
                          !!(alloc_flags & PMM_ALLOC_FLAG_CAN_BORROW);
  const bool must_borrow = can_borrow && !!(alloc_flags & PMM_ALLOC_FLAG_MUST_BORROW);

  AutoPreemptDisabler preempt_disable;
  Guard<Mutex> guard{&lock_};

  uint64_t free_count;
  if (must_borrow) {
    free_count = 0;
  } else {
    free_count = free_count_.load(ktl::memory_order_relaxed);
  }
  uint64_t available_count = free_count;
  uint64_t free_loaned_count = 0;
  if (can_borrow) {
    free_loaned_count = free_loaned_count_.load(ktl::memory_order_relaxed);
    available_count += free_loaned_count;
  }
  if (unlikely(count > available_count)) {
    if (!must_borrow) {
      // Allocation failures from the regular free list are likely to become user-visible.
      ReportAllocFailure();
    }
    return ZX_ERR_NO_MEMORY;
  }
  // Prefer to allocate from loaned, if allowed by this allocation.  If loaned is not allowed by
  // this allocation, free_loaned_count will be zero here.
  DEBUG_ASSERT(can_borrow || !free_loaned_count);
  DEBUG_ASSERT(!must_borrow || !free_count);
  uint64_t from_loaned_free = ktl::min(count, free_loaned_count);
  uint64_t from_free = count - from_loaned_free;

  DecrementFreeCountLocked(from_free);

  if (unlikely(InOomStateLocked())) {
    if (alloc_flags & PMM_ALLOC_DELAY_OK) {
      IncrementFreeCountLocked(from_free);
      // TODO(stevensd): Differentiate 'cannot allocate now' from 'can never allocate'
      return ZX_ERR_NO_MEMORY;
    }
  }

  DecrementFreeLoanedCountLocked(from_loaned_free);

  do {
    DEBUG_ASSERT(count == from_loaned_free + from_free);
    list_node* which_list;
    size_t which_count;
    if (can_borrow && !list_is_empty(&free_loaned_list_)) {
      which_list = &free_loaned_list_;
      which_count = from_loaned_free;
      from_loaned_free = 0;
    } else {
      DEBUG_ASSERT(!must_borrow);
      which_list = &free_list_;
      which_count = from_free;
      from_free = 0;
    }
    count -= which_count;

    DEBUG_ASSERT(which_count > 0);
    auto node = which_list;
    while (which_count > 0) {
      node = list_next(which_list, node);
      DEBUG_ASSERT(can_borrow || !containerof(node, vm_page, queue_node)->is_loaned());
      AllocPageHelperLocked(containerof(node, vm_page, queue_node));
      --which_count;
    }

    list_node tmp_list = LIST_INITIAL_VALUE(tmp_list);
    list_split_after(which_list, node, &tmp_list);
    if (list_is_empty(list)) {
      list_move(which_list, list);
    } else {
      list_splice_after(which_list, list_peek_tail(list));
    }
    list_move(&tmp_list, which_list);
    DEBUG_ASSERT(count == from_loaned_free + from_free);
  } while (count > 0);

  return ZX_OK;
}

zx_status_t PmmNode::AllocRange(paddr_t address, size_t count, list_node* list) {
  LTRACEF("address %#" PRIxPTR ", count %zu\n", address, count);

  DEBUG_ASSERT(Thread::Current::memory_allocation_state().IsEnabled());
  // list must be initialized prior to calling this
  DEBUG_ASSERT(list);
  // On error scenarios we will free the list, so make sure the caller didn't leave anything in
  // there.
  DEBUG_ASSERT(list_is_empty(list));

  size_t allocated = 0;
  if (count == 0) {
    return ZX_OK;
  }

  address = ROUNDDOWN(address, PAGE_SIZE);

  AutoPreemptDisabler preempt_disable;
  Guard<Mutex> guard{&lock_};

  // walk through the arenas, looking to see if the physical page belongs to it
  for (auto& a : arena_list_) {
    for (; allocated < count && a.address_in_arena(address); address += PAGE_SIZE) {
      vm_page_t* page = a.FindSpecific(address);
      if (!page) {
        break;
      }

      if (!page->is_free()) {
        break;
      }

      // We never allocate loaned pages for caller of AllocRange()
      if (page->loaned) {
        break;
      }

      list_delete(&page->queue_node);

      AllocPageHelperLocked(page);

      list_add_tail(list, &page->queue_node);

      allocated++;
      DecrementFreeCountLocked(1);
    }

    if (allocated == count) {
      break;
    }
  }

  if (allocated != count) {
    // we were not able to allocate the entire run, free these pages
    FreeListLocked(list);
    return ZX_ERR_NOT_FOUND;
  }

  return ZX_OK;
}

zx_status_t PmmNode::AllocContiguous(const size_t count, uint alloc_flags, uint8_t alignment_log2,
                                     paddr_t* pa, list_node* list) {
  DEBUG_ASSERT(Thread::Current::memory_allocation_state().IsEnabled());
  LTRACEF("count %zu, align %u\n", count, alignment_log2);

  if (count == 0) {
    return ZX_OK;
  }
  if (alignment_log2 < PAGE_SIZE_SHIFT) {
    alignment_log2 = PAGE_SIZE_SHIFT;
  }

  DEBUG_ASSERT(!(alloc_flags & (PMM_ALLOC_FLAG_CAN_BORROW | PMM_ALLOC_FLAG_MUST_BORROW)));
  // pa and list must be valid pointers
  DEBUG_ASSERT(pa);
  DEBUG_ASSERT(list);

  AutoPreemptDisabler preempt_disable;
  Guard<Mutex> guard{&lock_};

  for (auto& a : arena_list_) {
    vm_page_t* p = a.FindFreeContiguous(count, alignment_log2);
    if (!p) {
      continue;
    }

    *pa = p->paddr();

    // remove the pages from the run out of the free list
    for (size_t i = 0; i < count; i++, p++) {
      DEBUG_ASSERT_MSG(p->is_free(), "p %p state %u\n", p, static_cast<uint32_t>(p->state()));
      // Loaned pages are never returned by FindFreeContiguous() above.
      DEBUG_ASSERT(!p->loaned);
      DEBUG_ASSERT(list_in_list(&p->queue_node));

      list_delete(&p->queue_node);
      p->set_state(vm_page_state::ALLOC);

      DecrementFreeCountLocked(1);
      AsanUnpoisonPage(p);
      checker_.AssertPattern(p);

      list_add_tail(list, &p->queue_node);
    }

    return ZX_OK;
  }

  // We could potentially move contents of non-pinned pages out of the way for critical contiguous
  // allocations, but for now...
  LTRACEF("couldn't find run\n");
  return ZX_ERR_NOT_FOUND;
}

void PmmNode::FreePageHelperLocked(vm_page* page) {
  LTRACEF("page %p state %zu paddr %#" PRIxPTR "\n", page, VmPageStateIndex(page->state()),
          page->paddr());

  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->state() != vm_page_state::OBJECT || page->object.pin_count == 0);

  // mark it free
  page->set_state(vm_page_state::FREE);

  // Coming from OBJECT or ALLOC, this will only be true if the page was loaned (and may still be
  // loaned, but doesn't have to be currently loaned if the contiguous VMO the page was loaned from
  // was deleted during stack ownership).
  //
  // Coming from a state other than OBJECT or ALLOC, this currently won't be true, but if it were
  // true in future, it would only be because a state other than OBJECT or ALLOC has a (future)
  // field overlapping, in which case we do want to clear the invalid stack owner pointer value.
  // We'll be ok to clear this invalid stack owner after setting FREE previously (instead of
  // clearing before) because the stack owner is only read elsewhere for pages with an underlying
  // contiguous VMO owner (whether actually loaned at the time or not), and pages with an underlying
  // contiguous VMO owner can only be in FREE, ALLOC, OBJECT states, which all have this field, so
  // reading an invalid stack owner pointer elsewhere won't happen (there's a magic number canary
  // just in case though).  We could instead clear out any invalid stack owner pointer before
  // setting FREE above and have a shorter comment here, but there's no actual need for the extra
  // "if", so we just let this "if" handle it (especially since this whole paragraph is a
  // hypothetical future since there aren't any overlapping fields yet as of this comment).
  if (page->object.is_stack_owned()) {
    // Make FREE visible before lack of stack owner.
    ktl::atomic_thread_fence(ktl::memory_order_release);
    page->object.clear_stack_owner();
  }

  if (unlikely(free_fill_enabled_)) {
    checker_.FillPattern(page);
  }

  AsanPoisonPage(page, kAsanPmmFreeMagic);
}

void PmmNode::FreePage(vm_page* page) {
  AutoPreemptDisabler preempt_disable;
  Guard<Mutex> guard{&lock_};

  // pages freed individually shouldn't be in a queue
  DEBUG_ASSERT(!list_in_list(&page->queue_node));

  FreePageHelperLocked(page);

  list_node* which_list = nullptr;
  if (!page->loaned) {
    IncrementFreeCountLocked(1);
    which_list = &free_list_;
  } else if (!page->loan_cancelled) {
    IncrementFreeLoanedCountLocked(1);
    which_list = &free_loaned_list_;
  }

  // Add the page to the appropriate free queue, unless loan_cancelled.  The loan_cancelled pages
  // don't go in any free queue because they shouldn't get re-used until reclaimed by their
  // underlying contiguous VMO or until that underlying contiguous VMO is deleted.
  DEBUG_ASSERT(which_list || page->loan_cancelled);
  if (which_list) {
    if constexpr (!__has_feature(address_sanitizer)) {
      list_add_head(which_list, &page->queue_node);
    } else {
      // If address sanitizer is enabled, put the page at the tail to maximize reuse distance.
      list_add_tail(which_list, &page->queue_node);
    }
  }
}

void PmmNode::FreeListLocked(list_node* list) {
  DEBUG_ASSERT(list);

  // process list backwards so the head is as hot as possible
  uint64_t count = 0;
  uint64_t loaned_count = 0;
  list_node freed_loaned_list = LIST_INITIAL_VALUE(freed_loaned_list);
  {  // scope page
    vm_page* page = list_peek_tail_type(list, vm_page_t, queue_node);
    while (page) {
      FreePageHelperLocked(page);
      vm_page_t* next_page = list_prev_type(list, &page->queue_node, vm_page_t, queue_node);
      if (page->loaned) {
        // Remove from |list| and possibly put on freed_loaned_list instead, to route to the correct
        // free list, or no free list if loan_cancelled.
        list_delete(&page->queue_node);
        if (!page->loan_cancelled) {
          list_add_head(&freed_loaned_list, &page->queue_node);
          ++loaned_count;
        }
      } else {
        count++;
      }
      page = next_page;
    }
  }  // end scope page

  if constexpr (!__has_feature(address_sanitizer)) {
    // splice list at the head of free_list_; free_loaned_list_.
    list_splice_after(list, &free_list_);
    list_splice_after(&freed_loaned_list, &free_loaned_list_);
  } else {
    // If address sanitizer is enabled, put the pages at the tail to maximize reuse distance.
    if (!list_is_empty(&free_list_)) {
      list_splice_after(list, list_peek_tail(&free_list_));
    } else {
      list_splice_after(list, &free_list_);
    }
    if (!list_is_empty(&free_loaned_list_)) {
      list_splice_after(&freed_loaned_list, list_peek_tail(&free_loaned_list_));
    } else {
      list_splice_after(&freed_loaned_list, &free_loaned_list_);
    }
  }

  IncrementFreeCountLocked(count);
  IncrementFreeLoanedCountLocked(loaned_count);
}

void PmmNode::FreeList(list_node* list) {
  AutoPreemptDisabler preempt_disable;
  Guard<Mutex> guard{&lock_};

  FreeListLocked(list);
}

bool PmmNode::InOomStateLocked() {
  if (mem_avail_state_cur_index_ == 0) {
    return true;
  }

#if RANDOM_DELAYED_ALLOC
  // Randomly try to make 10% of allocations delayed allocations.
  return rand() < (RAND_MAX / 10);
#else
  return false;
#endif
}

uint64_t PmmNode::CountFreePages() const TA_NO_THREAD_SAFETY_ANALYSIS {
  return free_count_.load(ktl::memory_order_relaxed);
}

uint64_t PmmNode::CountLoanedFreePages() const TA_NO_THREAD_SAFETY_ANALYSIS {
  return free_loaned_count_.load(ktl::memory_order_relaxed);
}

uint64_t PmmNode::CountLoanedNotFreePages() const TA_NO_THREAD_SAFETY_ANALYSIS {
  AutoPreemptDisabler preempt_disable;
  Guard<Mutex> guard{&lock_};
  return loaned_count_.load(ktl::memory_order_relaxed) -
         free_loaned_count_.load(ktl::memory_order_relaxed);
}

uint64_t PmmNode::CountLoanedPages() const TA_NO_THREAD_SAFETY_ANALYSIS {
  return loaned_count_.load(ktl::memory_order_relaxed);
}

uint64_t PmmNode::CountLoanCancelledPages() const TA_NO_THREAD_SAFETY_ANALYSIS {
  return loan_cancelled_count_.load(ktl::memory_order_relaxed);
}

uint64_t PmmNode::CountTotalBytes() const TA_NO_THREAD_SAFETY_ANALYSIS {
  return arena_cumulative_size_;
}

void PmmNode::DumpFree() const TA_NO_THREAD_SAFETY_ANALYSIS {
  auto megabytes_free = CountFreePages() * PAGE_SIZE / MB;
  printf(" %zu free MBs\n", megabytes_free);
}

void PmmNode::Dump(bool is_panic) const {
  // No lock analysis here, as we want to just go for it in the panic case without the lock.
  auto dump = [this]() TA_NO_THREAD_SAFETY_ANALYSIS {
    uint64_t free_count = free_count_.load(ktl::memory_order_relaxed);
    uint64_t free_loaned_count = free_loaned_count_.load(ktl::memory_order_relaxed);
    printf(
        "pmm node %p: free_count %zu (%zu bytes), free_loaned_count: %zu (%zu bytes), total size "
        "%zu\n",
        this, free_count, free_count * PAGE_SIZE, free_loaned_count, free_loaned_count * PAGE_SIZE,
        arena_cumulative_size_);
    for (auto& a : arena_list_) {
      a.Dump(false, false);
    }
  };

  if (is_panic) {
    dump();
  } else {
    Guard<Mutex> guard{&lock_};
    dump();
  }
}

zx_status_t PmmNode::InitReclamation(const uint64_t* watermarks, uint8_t watermark_count,
                                     uint64_t debounce, void* context,
                                     mem_avail_state_updated_callback_t callback) {
  if (watermark_count > MAX_WATERMARK_COUNT) {
    return ZX_ERR_INVALID_ARGS;
  }

  AutoPreemptDisabler preempt_disable;
  Guard<Mutex> guard{&lock_};

  uint64_t tmp[MAX_WATERMARK_COUNT];
  uint64_t tmp_debounce = fbl::round_up(debounce, static_cast<uint64_t>(PAGE_SIZE)) / PAGE_SIZE;
  for (uint8_t i = 0; i < watermark_count; i++) {
    tmp[i] = watermarks[i] / PAGE_SIZE;
    if (i > 0) {
      if (tmp[i] <= tmp[i - 1]) {
        return ZX_ERR_INVALID_ARGS;
      }
    } else {
      if (tmp[i] < tmp_debounce) {
        return ZX_ERR_INVALID_ARGS;
      }
    }
  }

  mem_avail_state_watermark_count_ = watermark_count;
  mem_avail_state_debounce_ = tmp_debounce;
  mem_avail_state_context_ = context;
  mem_avail_state_callback_ = callback;
  memcpy(mem_avail_state_watermarks_, tmp, sizeof(mem_avail_state_watermarks_));
  static_assert(sizeof(tmp) == sizeof(mem_avail_state_watermarks_));

  UpdateMemAvailStateLocked();

  return ZX_OK;
}

void PmmNode::UpdateMemAvailStateLocked() {
  // Find the smallest watermark which is greater than the number of free pages.
  uint8_t target = mem_avail_state_watermark_count_;
  for (uint8_t i = 0; i < mem_avail_state_watermark_count_; i++) {
    if (mem_avail_state_watermarks_[i] > free_count_.load(ktl::memory_order_relaxed)) {
      target = i;
      break;
    }
  }
  SetMemAvailStateLocked(target);
}

void PmmNode::SetMemAvailStateLocked(uint8_t mem_avail_state) {
  mem_avail_state_cur_index_ = mem_avail_state;

  if (mem_avail_state_cur_index_ == 0) {
    free_pages_evt_.Unsignal();
  } else {
    free_pages_evt_.Signal();
  }

  if (mem_avail_state_cur_index_ > 0) {
    // If there is a smaller watermark, then we transition into that state when the
    // number of free pages drops more than |mem_avail_state_debounce_| pages into that state.
    mem_avail_state_lower_bound_ =
        mem_avail_state_watermarks_[mem_avail_state_cur_index_ - 1] - mem_avail_state_debounce_;
  } else {
    // There is no smaller state, so we can't ever transition down.
    mem_avail_state_lower_bound_ = 0;
  }

  if (mem_avail_state_cur_index_ < mem_avail_state_watermark_count_) {
    // If there is a larger watermark, then we transition out of the current state when
    // the number of free pages exceedes the current state's watermark by at least
    // |mem_avail_state_debounce_|.
    mem_avail_state_upper_bound_ =
        mem_avail_state_watermarks_[mem_avail_state_cur_index_] + mem_avail_state_debounce_;
  } else {
    // There is no larger state, so we can't ever transition up.
    mem_avail_state_upper_bound_ = UINT64_MAX / PAGE_SIZE;
  }

  mem_avail_state_callback_(mem_avail_state_context_, mem_avail_state_cur_index_);
}

void PmmNode::DumpMemAvailState() const {
  Guard<Mutex> guard{&lock_};

  printf("watermarks: [");
  for (unsigned i = 0; i < mem_avail_state_watermark_count_; i++) {
    printf("%s%s", FormattedBytes(mem_avail_state_watermarks_[i] * PAGE_SIZE).c_str(),
           i + 1 == mem_avail_state_watermark_count_ ? "]\n" : ", ");
  }
  printf("debounce: %s\n", FormattedBytes(mem_avail_state_debounce_ * PAGE_SIZE).c_str());
  printf("current state: %u\n", mem_avail_state_cur_index_);
  printf("current bounds: [%s, %s]\n",
         FormattedBytes(mem_avail_state_lower_bound_ * PAGE_SIZE).c_str(),
         FormattedBytes(mem_avail_state_upper_bound_ * PAGE_SIZE).c_str());
  printf("free memory: %s\n", FormattedBytes(free_count_ * PAGE_SIZE).c_str());
}

uint64_t PmmNode::DebugNumPagesTillMemState(uint8_t mem_state_idx) const {
  Guard<Mutex> guard{&lock_};
  if (mem_avail_state_cur_index_ <= mem_state_idx) {
    // Already in mem_state_idx, or in a state with less available memory than mem_state_idx.
    return 0;
  }
  // We need to either get free_pages below mem_avail_state_watermarks_[mem_state_idx] or, if we are
  // in state (mem_state_idx + 1), we also need to clear the debounce amount. For simplicity we just
  // always allocate the debounce amount as well.
  uint64_t trigger = mem_avail_state_watermarks_[mem_state_idx] - mem_avail_state_debounce_;
  return (free_count_ - trigger);
}

uint8_t PmmNode::DebugMaxMemAvailState() const {
  Guard<Mutex> guard{&lock_};
  return mem_avail_state_watermark_count_;
}

void PmmNode::DebugMemAvailStateCallback(uint8_t mem_state_idx) const {
  Guard<Mutex> guard{&lock_};
  if (mem_state_idx >= mem_avail_state_watermark_count_) {
    return;
  }
  // Invoke callback for the requested state without allocating additional memory, or messing with
  // any of the internal memory state tracking counters.
  mem_avail_state_callback_(mem_avail_state_context_, mem_state_idx);
}

int64_t PmmNode::get_alloc_failed_count() { return pmm_alloc_failed.Value(); }

bool PmmNode::has_alloc_failed_no_mem() {
  return alloc_failed_no_mem.load(ktl::memory_order_relaxed);
}

void PmmNode::BeginLoan(list_node* page_list) {
  DEBUG_ASSERT(page_list);
  AutoPreemptDisabler preempt_disable;
  Guard<Mutex> guard{&lock_};

  uint64_t loaned_count = 0;
  vm_page* page;
  list_for_every_entry (page_list, page, vm_page, queue_node) {
    DEBUG_ASSERT(!page->loaned);
    DEBUG_ASSERT(!page->is_free());
    page->loaned = true;
    ++loaned_count;
    DEBUG_ASSERT(!page->loan_cancelled);
  }
  IncrementLoanedCountLocked(loaned_count);

  // Callers of BeginLoan() generally won't want the pages loaned to them; the intent is to loan to
  // the rest of the system, so go ahead and free also.  Some callers will basically choose between
  // pmm_begin_loan() and pmm_free().
  FreeListLocked(page_list);
}

void PmmNode::CancelLoan(paddr_t address, size_t count) {
  AutoPreemptDisabler preempt_disable;
  Guard<Mutex> guard{&lock_};
  DEBUG_ASSERT(IS_PAGE_ALIGNED(address));
  paddr_t end = address + count * PAGE_SIZE;
  DEBUG_ASSERT(address <= end);

  uint64_t loan_cancelled_count = 0;
  uint64_t no_longer_free_loaned_count = 0;

  ForPagesInPhysRangeLocked(address, count,
                            [&loan_cancelled_count, &no_longer_free_loaned_count](vm_page_t* page) {
                              // We can assert this because of PageSource's overlapping request
                              // handling.
                              DEBUG_ASSERT(page->is_loaned());
                              bool was_cancelled = page->loan_cancelled;
                              // We can assert this because of PageSource's overlapping request
                              // handling.
                              DEBUG_ASSERT(!was_cancelled);
                              page->loan_cancelled = true;
                              ++loan_cancelled_count;
                              if (page->is_free()) {
                                // Currently in free_loaned_list_.
                                DEBUG_ASSERT(list_in_list(&page->queue_node));
                                // Remove from free_loaned_list_ to prevent any new use until
                                // after EndLoan.
                                list_delete(&page->queue_node);
                                no_longer_free_loaned_count++;
                              }
                            });

  IncrementLoanCancelledCountLocked(loan_cancelled_count);
  DecrementFreeLoanedCountLocked(no_longer_free_loaned_count);
}

void PmmNode::EndLoan(paddr_t address, size_t count, list_node* page_list) {
  AutoPreemptDisabler preempt_disable;
  Guard<Mutex> guard{&lock_};
  DEBUG_ASSERT(IS_PAGE_ALIGNED(address));
  paddr_t end = address + count * PAGE_SIZE;
  DEBUG_ASSERT(address <= end);

  uint64_t loan_ended_count = 0;

  ForPagesInPhysRangeLocked(address, count, [this, &page_list, &loan_ended_count](vm_page_t* page) {
    AssertHeld(lock_);

    // PageSource serializing such that there's only one request to PageProvider in flight at a time
    // for any given page is the main reason we can assert these instead of needing to check these.
    DEBUG_ASSERT(page->is_loaned());
    DEBUG_ASSERT(page->is_loan_cancelled());
    DEBUG_ASSERT(page->is_free());

    // Already not in free_loaned_list_ (because loan_cancelled already).
    DEBUG_ASSERT(!list_in_list(&page->queue_node));

    page->loaned = false;
    page->loan_cancelled = false;
    ++loan_ended_count;

    AllocPageHelperLocked(page);
    list_add_tail(page_list, &page->queue_node);
  });

  DecrementLoanCancelledCountLocked(loan_ended_count);
  DecrementLoanedCountLocked(loan_ended_count);
}

void PmmNode::DeleteLender(paddr_t address, size_t count) {
  AutoPreemptDisabler preempt_disable;
  Guard<Mutex> guard{&lock_};
  DEBUG_ASSERT(IS_PAGE_ALIGNED(address));
  paddr_t end = address + count * PAGE_SIZE;
  DEBUG_ASSERT(address <= end);
  uint64_t removed_free_loaned_count = 0;
  uint64_t added_free_count = 0;

  uint64_t loan_ended_count = 0;
  uint64_t loan_un_cancelled_count = 0;

  ForPagesInPhysRangeLocked(address, count,
                            [this, &removed_free_loaned_count, &loan_un_cancelled_count,
                             &added_free_count, &loan_ended_count](vm_page_t* page) {
                              DEBUG_ASSERT(page->loaned);
                              if (page->is_free() && !page->loan_cancelled) {
                                // Remove from free_loaned_list_.
                                list_delete(&page->queue_node);
                                ++removed_free_loaned_count;
                              }
                              if (page->loan_cancelled) {
                                ++loan_un_cancelled_count;
                              }
                              if (page->is_free()) {
                                // add it to the free queue
                                if constexpr (!__has_feature(address_sanitizer)) {
                                  list_add_head(&free_list_, &page->queue_node);
                                } else {
                                  // If address sanitizer is enabled, put the page at the tail to
                                  // maximize reuse distance.
                                  list_add_tail(&free_list_, &page->queue_node);
                                }
                                added_free_count++;
                              }
                              page->loan_cancelled = false;
                              page->loaned = false;
                              ++loan_ended_count;
                            });

  DecrementFreeLoanedCountLocked(removed_free_loaned_count);
  IncrementFreeCountLocked(added_free_count);
  DecrementLoanedCountLocked(loan_ended_count);
  DecrementLoanCancelledCountLocked(loan_un_cancelled_count);
}

bool PmmNode::IsLoaned(vm_page_t* page) {
  AutoPreemptDisabler preempt_disable;
  Guard<Mutex> guard{&lock_};
  return page->loaned;
}

template <typename F>
void PmmNode::ForPagesInPhysRangeLocked(paddr_t start, size_t count, F func) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(start));
  // We only intend ForPagesInRange() to be used after arenas have been added to the global
  // pmm_node.
  DEBUG_ASSERT(mp_get_active_mask() != 0);

  if (unlikely(arena_list_.is_empty())) {
    // We're in a unit test, using ManagedPmmNode which has no arenas.  So fall back to the global
    // pmm_node (which has at least one arena) to find the actual vm_page_t for each page.
    //
    // TODO: Make ManagedPmmNode have a more real arena, possibly by allocating a contiguous VMO and
    // creating an arena from that.
    paddr_t end = start + count * PAGE_SIZE;
    for (paddr_t iter = start; iter < end; iter += PAGE_SIZE) {
      vm_page_t* page = paddr_to_vm_page(iter);
      func(page);
    }
    return;
  }

  // We have at least one arena, so use arena_list_ directly.
  paddr_t end = start + count * PAGE_SIZE;
  DEBUG_ASSERT(start <= end);
  paddr_t page_addr = start;
  for (auto& a : arena_list_) {
    for (; page_addr < end && a.address_in_arena(page_addr); page_addr += PAGE_SIZE) {
      vm_page_t* page = a.FindSpecific(page_addr);
      DEBUG_ASSERT(page);
      DEBUG_ASSERT(page_addr == page->paddr());
      func(page);
    }
    if (page_addr == end) {
      break;
    }
  }
  DEBUG_ASSERT(page_addr == end);
}

void PmmNode::ReportAllocFailure() {
  kcounter_add(pmm_alloc_failed, 1);

  // Update before signaling the MemoryWatchdog to ensure it observes the update.
  //
  // |alloc_failed_no_mem| latches so only need to invoke the callback once.  We could call it on
  // every failure, but that's wasteful and we don't want to spam any underlying Event (or the
  // thread lock or the MemoryWatchdog).
  const bool first_time = !alloc_failed_no_mem.exchange(true, ktl::memory_order_relaxed);
  if (first_time) {
    // Note, the |cur_state| value passed to the callback doesn't really matter because all we're
    // trying to do here is signal and unblock the MemoryWatchdog's worker thread.
    mem_avail_state_callback_(mem_avail_state_context_, mem_avail_state_cur_index_);
  }
}
