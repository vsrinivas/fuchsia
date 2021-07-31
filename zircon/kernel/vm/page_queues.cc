// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fbl/ref_counted_upgradeable.h>
#include <vm/page_queues.h>
#include <vm/vm_cow_pages.h>
#include "include/vm/page.h"
#include "include/vm/pmm.h"
#include <kernel/auto_preempt_disabler.h>
#include <object/thread_dispatcher.h>

PageQueues::PageQueues() {
  for (uint32_t i = 0; i < PageQueueNumQueues; i++) {
    list_initialize(&page_queues_[i]);
  }
}

PageQueues::~PageQueues() {
  for (uint32_t i = 0; i < PageQueueNumQueues; i++) {
    DEBUG_ASSERT(list_is_empty(&page_queues_[i]));
  }
  for (size_t i = 0; i < page_queue_counts_.size(); i++) {
    DEBUG_ASSERT_MSG(page_queue_counts_[i] == 0, "i=%zu count=%zu", i,
                     page_queue_counts_[i].load());
  }
}

void PageQueues::RotatePagerBackedQueues() {
  // We want to increment mru_gen, but first may need to make space by incrementing lru gen.
  if (mru_gen_.load(ktl::memory_order_relaxed) - lru_gen_.load(ktl::memory_order_relaxed) ==
      kNumPagerBacked - 1) {
    // Process the LRU queue until we have at least one slot free.
    ProcessLruQueue(mru_gen_.load(ktl::memory_order_relaxed) - (kNumPagerBacked - 2), false);
  }
  // Now that we know there is space, can move the mru queue.
  mru_gen_.fetch_add(1, ktl::memory_order_relaxed);
}

ktl::optional<PageQueues::VmoBacklink> PageQueues::ProcessLruQueue(uint64_t target_gen, bool peek) {
  // This assertion is <=, and not strictly <, since to evict a some queue X, the target must be
  // X+1. Hence to preserve kNumActiveQueues, we can allow target_gen to become equal to the first
  // active queue, as this will process all the non-active queues.
  ASSERT(target_gen <= mru_gen_.load(ktl::memory_order_relaxed) - (kNumActiveQueues - 1));

  const PageQueue mru_queue = mru_gen_to_queue();

  // Processing the lru queue requires holding the page_queues_ lock_. The only other actions that
  // require this lock are inserting or removing pages from the page queues. To ensure these actions
  // can complete in a small bounded time kMaxQueueWork is chosen to be very small so that the lock
  // will be regularly dropped. As processing the lru queue is not time critical and can be somewhat
  // inefficient in its operation we err on the side of doing less work per lock acquisition.
  static constexpr uint32_t kMaxQueueWork = 32;

  for (uint64_t lru = lru_gen_.load(ktl::memory_order_relaxed); lru < target_gen;
       lru = lru_gen_.load(ktl::memory_order_relaxed)) {
    Guard<CriticalMutex> guard{&lock_};
    PageQueue queue = gen_to_queue(lru);
    uint32_t work_remain = kMaxQueueWork;
    while (!list_is_empty(&page_queues_[queue]) && work_remain > 0) {
      work_remain--;
      // Process the list from its notional oldest (tail) to notional newest (head)
      vm_page_t* page = list_peek_tail_type(&page_queues_[queue], vm_page_t, queue_node);
      PageQueue page_queue =
          (PageQueue)page->object.get_page_queue_ref().load(fbl::memory_order_relaxed);
      DEBUG_ASSERT(page_queue >= PageQueuePagerBackedBase);
      // If the queue stored in the page does not match then we want to move it to its correct queue
      // with the caveat that its queue could be invalid. The queue would be invalid if MarkAccessed
      // had raced. Should this happen we know that the page is actually *very* old, and so we will
      // fall back to the case of forcibly changing its age to the new lru gen.
      if (page_queue != queue && queue_is_valid(page_queue, queue, mru_queue)) {
        list_delete(&page->queue_node);
        list_add_head(&page_queues_[page_queue], &page->queue_node);
      } else if (peek) {
        VmCowPages* cow = reinterpret_cast<VmCowPages*>(page->object.get_object());
        uint64_t page_offset = page->object.get_page_offset();
        DEBUG_ASSERT(cow);

        // We may be racing with destruction of VMO. As we currently hold our lock we know that our
        // back pointer is correct in so far as the VmCowPages has not yet had completed running its
        // destructor, so we know it is safe to attempt to upgrade it to a RefPtr. If upgrading
        // fails we assume the page is about to be removed from the page queue once the VMO
        // destructor gets a chance to run.
        return VmoBacklink{fbl::MakeRefPtrUpgradeFromRaw(cow, lock_), page, page_offset};
      } else {
        // Force it into our target queue, don't care about races. If we happened to access it at
        // the same time then too bad.
        PageQueue new_queue = gen_to_queue(lru + 1);
        PageQueue old_queue = (PageQueue)page->object.get_page_queue_ref().exchange(new_queue);
        DEBUG_ASSERT(old_queue >= PageQueuePagerBackedBase);
        page_queue_counts_[old_queue].fetch_sub(1, ktl::memory_order_relaxed);
        page_queue_counts_[new_queue].fetch_add(1, ktl::memory_order_relaxed);
        list_delete(&page->queue_node);
        list_add_head(&page_queues_[new_queue], &page->queue_node);
      }
    }
    if (list_is_empty(&page_queues_[queue])) {
      lru_gen_.store(lru + 1, ktl::memory_order_relaxed);
    }
  }

  return ktl::nullopt;
}

void PageQueues::SetQueueLocked(vm_page_t* page, PageQueue queue) {
  SetQueueBacklinkLocked(page, nullptr, 0, queue);
}

void PageQueues::SetQueueBacklinkLocked(vm_page_t* page, void* object, uintptr_t page_offset,
                                        PageQueue queue) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(!list_in_list(&page->queue_node));
  if (object) {
    DEBUG_ASSERT(!page->object.get_object() || page->object.get_object() == object);
    DEBUG_ASSERT(!page->object.get_object() || page->object.get_page_offset() == page_offset);
    page->object.set_object(object);
    page->object.set_page_offset(page_offset);
  }
  DEBUG_ASSERT(page->object.get_page_queue_ref().load(fbl::memory_order_relaxed) == PageQueueNone);
  page->object.get_page_queue_ref().store(queue, fbl::memory_order_relaxed);
  list_add_head(&page_queues_[queue], &page->queue_node);
  page_queue_counts_[queue].fetch_add(1, ktl::memory_order_relaxed);
}

void PageQueues::MoveToQueueLocked(vm_page_t* page, PageQueue queue) {
  MoveToQueueBacklinkLocked(page, nullptr, 0, queue);
}

void PageQueues::MoveToQueueBacklinkLocked(vm_page_t* page, void* object, uintptr_t page_offset,
                                           PageQueue queue) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  uint32_t old_queue = page->object.get_page_queue_ref().exchange(queue, fbl::memory_order_relaxed);
  DEBUG_ASSERT(old_queue != PageQueueNone);
  DEBUG_ASSERT(old_queue != PageQueueWired || queue != PageQueueWired);
  if (old_queue == PageQueueWired) {
    zx_time_t now = current_time();
    uint32_t retro_now = now / kUnpinTimeResolutionNanos % VM_PAGE_OBJECT_UNPIN_TIME_MODULUS;
    DEBUG_ASSERT(retro_now <= VM_PAGE_OBJECT_MAX_UNPIN_TIME);
    page->object.set_unpin_time(static_cast<uint8_t>(retro_now));
  }
  if (object) {
    DEBUG_ASSERT(!page->object.get_object() || page->object.get_object() == object);
    DEBUG_ASSERT(!page->object.get_object() || page->object.get_page_offset() == page_offset);
    page->object.set_object(object);
    page->object.set_page_offset(page_offset);
  }
  list_delete(&page->queue_node);
  list_add_head(&page_queues_[queue], &page->queue_node);
  page_queue_counts_[old_queue].fetch_sub(1, ktl::memory_order_relaxed);
  page_queue_counts_[queue].fetch_add(1, ktl::memory_order_relaxed);
}

void PageQueues::SetBacklinkLocked(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  page->object.set_object(object);
  page->object.set_page_offset(page_offset);
}

void PageQueues::ClearBacklinkLocked(vm_page_t* page) {
  if (page->state() != vm_page_state::OBJECT) {
    dprintf(INFO, "unexpected state: %hhu\n", page->state());
  }
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  // DO NOT SUBMIT - see if we can uncomment this.
  //DEBUG_ASSERT(page->object.get_object());
  page->object.set_object(nullptr);
  page->object.set_page_offset(0);
}

void PageQueues::SetNewPageBacklink(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  Guard<CriticalMutex> guard{&lock_};
  // This is done under lock_ to keep sync with page->object.add_event and related.
  page->set_state(vm_page_state::OBJECT);
  SetBacklinkLocked(page, object, page_offset);
  // Fake unpin times shouldn't tend to be clustered around the same value.  If they do cluster, the
  // only downside is inability for low memory continuous LoanSweeper to move contents of a
  // non-loaned pages into a loaned page, until the unpin time appears to be far enough in the past.
  // The sychronous sweep near OOM will move even if the page seems to have been unpinned quite
  // recently.
  page->object.set_unpin_time(next_arbitrary_unpin_time_);
  next_arbitrary_unpin_time_ += kArbitraryUnpinTimeCounterIncrement;
  next_arbitrary_unpin_time_ %= VM_PAGE_OBJECT_UNPIN_TIME_MODULUS;
}

void PageQueues::SetPageBacklink(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  DEBUG_ASSERT(page);
  DEBUG_ASSERT(object);
  Guard<CriticalMutex> guard{&lock_};
  SetBacklinkLocked(page, object, page_offset);
}

void PageQueues::ClearPageBacklink(vm_page_t* page) {
  DEBUG_ASSERT(page);
  Guard<CriticalMutex> guard{&lock_};
  ClearBacklinkLocked(page);
}

void PageQueues::SetWired(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  SetQueueLocked(page, PageQueueWired);
}

void PageQueues::MoveToWired(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToQueueLocked(page, PageQueueWired);
}

void PageQueues::SetUnswappable(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  SetQueueLocked(page, PageQueueUnswappable);
}

void PageQueues::MoveToUnswappableLocked(vm_page_t* page) {
  MoveToQueueLocked(page, PageQueueUnswappable);
}

void PageQueues::MoveToUnswappable(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToUnswappableLocked(page);
}

void PageQueues::SetPagerBacked(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  Guard<CriticalMutex> guard{&lock_};
  SetQueueBacklinkLocked(page, object, page_offset, mru_gen_to_queue());
}

void PageQueues::MoveToPagerBacked(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToQueueBacklinkLocked(page, object, page_offset, mru_gen_to_queue());
}

void PageQueues::MoveToPagerBackedInactive(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToQueueBacklinkLocked(page, page->object.get_object(), page->object.get_page_offset(),
                            PageQueuePagerBackedInactive);
}

void PageQueues::SetUnswappableZeroFork(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  Guard<CriticalMutex> guard{&lock_};
  SetQueueBacklinkLocked(page, object, page_offset, PageQueueUnswappableZeroFork);
}

void PageQueues::MoveToUnswappableZeroFork(vm_page_t* page, VmCowPages* object,
                                           uint64_t page_offset) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToQueueBacklinkLocked(page, object, page_offset, PageQueueUnswappableZeroFork);
}

void PageQueues::RemoveLocked(vm_page_t* page) {
  // Directly exchange the old gen.
  uint32_t old_queue =
      page->object.get_page_queue_ref().exchange(PageQueueNone, fbl::memory_order_relaxed);
  DEBUG_ASSERT(old_queue != PageQueueNone);
  DEBUG_ASSERT(old_queue != PageQueueWired);
  page_queue_counts_[old_queue].fetch_sub(1, ktl::memory_order_relaxed);
  page->object.set_object(nullptr);
  page->object.set_page_offset(0);
  list_delete(&page->queue_node);
}

void PageQueues::Remove(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  RemoveLocked(page);
}

void PageQueues::RemoveArrayIntoList(vm_page_t** pages, size_t count, list_node_t* out_list) {
  DEBUG_ASSERT(pages);
  Guard<CriticalMutex> guard{&lock_};
  for (size_t i = 0; i < count; i++) {
    DEBUG_ASSERT(pages[i]);
    RemoveLocked(pages[i]);
    list_add_tail(out_list, &pages[i]->queue_node);
  }
}

PageQueues::PagerCounts PageQueues::GetPagerQueueCounts() const {
  PagerCounts counts;

  // Grab the lock to prevent LRU processing, this lets us get a slightly less racy snapshot of the
  // queue counts, although we may still double count pages that move after we count them.
  // Specifically any parallel callers of MarkAccessed could move a page and change the counts,
  // causing us to either double count or miss count that page. As these counts are not load bearing
  // we accept the very small chance of potentially being off a few pages.
  uint32_t lru = lru_gen_.load(ktl::memory_order_relaxed);
  uint32_t mru = mru_gen_.load(ktl::memory_order_relaxed);
  Guard<CriticalMutex> guard{&lock_};

  counts.total = 0;
  for (uint32_t index = lru; index <= mru; index++) {
    // Distance to the MRU determines the bucket the count goes into, with 'newest' having a
    // distance of 0 and 'oldest' having a distance of kNumPagerBacked - 1.
    uint32_t age = mru - index;
    uint64_t count = page_queue_counts_[gen_to_queue(index)].load(ktl::memory_order_relaxed);
    if (age == 0) {
      counts.newest = count;
    } else if (age == kNumPagerBacked - 1) {
      counts.oldest = count;
    }
    counts.total += count;
  }
  // Account the inactive queue length under |oldest|, since (inactive + oldest LRU) pages are
  // eligible for reclamation first. |oldest| is meant to track pages eligible for eviction first.
  uint64_t inactive_count =
      page_queue_counts_[PageQueuePagerBackedInactive].load(ktl::memory_order_relaxed);
  counts.oldest += inactive_count;
  counts.total += inactive_count;
  return counts;
}

PageQueues::Counts PageQueues::QueueCounts() const {
  Counts counts = {};

  // Grab the lock to prevent LRU processing, this lets us get a slightly less racy snapshot of the
  // queue counts. We may still double count pages that move after we count them.
  uint32_t lru = lru_gen_.load(ktl::memory_order_relaxed);
  uint32_t mru = mru_gen_.load(ktl::memory_order_relaxed);
  Guard<CriticalMutex> guard{&lock_};

  for (uint32_t index = lru; index <= mru; index++) {
    counts.pager_backed[mru - index] =
        page_queue_counts_[gen_to_queue(index)].load(ktl::memory_order_relaxed);
  }
  counts.pager_backed_inactive =
      page_queue_counts_[PageQueuePagerBackedInactive].load(ktl::memory_order_relaxed);
  counts.unswappable = page_queue_counts_[PageQueueUnswappable].load(ktl::memory_order_relaxed);
  counts.wired = page_queue_counts_[PageQueueWired].load(ktl::memory_order_relaxed);
  counts.unswappable_zero_fork =
      page_queue_counts_[PageQueueUnswappableZeroFork].load(ktl::memory_order_relaxed);
  return counts;
}

bool PageQueues::DebugPageIsPagerBacked(const vm_page_t* page, size_t* queue) const {
  PageQueue q = (PageQueue)page->object.get_page_queue_ref().load(fbl::memory_order_relaxed);
  if (q >= PageQueuePagerBackedBase && q <= PageQueuePagerBackedLast) {
    if (queue) {
      PageQueue mru_queue = mru_gen_to_queue();
      if (q <= mru_queue) {
        *queue = mru_queue - q;
      } else {
        *queue = (kNumPagerBacked - q) + mru_queue;
      }
    }
    return true;
  }
  return false;
}

bool PageQueues::DebugPageIsPagerBackedInactive(const vm_page_t* page) const {
  return page->object.get_page_queue_ref().load(fbl::memory_order_relaxed) ==
         PageQueuePagerBackedInactive;
}

bool PageQueues::DebugPageIsUnswappable(const vm_page_t* page) const {
  return page->object.get_page_queue_ref().load(fbl::memory_order_relaxed) == PageQueueUnswappable;
}

bool PageQueues::DebugPageIsWired(const vm_page_t* page) const {
  return page->object.get_page_queue_ref().load(fbl::memory_order_relaxed) == PageQueueWired;
}

bool PageQueues::DebugPageIsUnswappableZeroFork(const vm_page_t* page) const {
  return page->object.get_page_queue_ref().load(fbl::memory_order_relaxed) ==
         PageQueueUnswappableZeroFork;
}

bool PageQueues::DebugPageIsAnyUnswappable(const vm_page_t* page) const {
  return DebugPageIsUnswappable(page) || DebugPageIsUnswappableZeroFork(page);
}

ktl::optional<PageQueues::VmoBacklink> PageQueues::PopUnswappableZeroFork() {
  Guard<CriticalMutex> guard{&lock_};

  vm_page_t* page =
      list_peek_tail_type(&page_queues_[PageQueueUnswappableZeroFork], vm_page_t, queue_node);
  if (!page) {
    return ktl::nullopt;
  }

  VmCowPages* cow = reinterpret_cast<VmCowPages*>(page->object.get_object());
  uint64_t page_offset = page->object.get_page_offset();
  DEBUG_ASSERT(cow);
  MoveToQueueLocked(page, PageQueueUnswappable);

  // We may be racing with destruction of VMO. As we currently hold our lock we know that our
  // back pointer is correct in so far as the VmCowPages has not yet had completed running its
  // destructor, so we know it is safe to attempt to upgrade it to a RefPtr. If upgrading fails
  // we assume the page is about to be removed from the page queue once the VMO destructor gets
  // a chance to run.
  return VmoBacklink{fbl::MakeRefPtrUpgradeFromRaw(cow, guard), page, page_offset};
}

ktl::optional<PageQueues::VmoBacklink> PageQueues::PeekPagerBacked(size_t lowest_queue) {
  {
    Guard<CriticalMutex> guard{&lock_};
    // Peek the tail of the inactive queue first.
    vm_page_t* page =
        list_peek_tail_type(&page_queues_[PageQueuePagerBackedInactive], vm_page_t, queue_node);
    if (page) {
      VmCowPages* cow = reinterpret_cast<VmCowPages*>(page->object.get_object());
      uint64_t page_offset = page->object.get_page_offset();
      DEBUG_ASSERT(cow);

      // We may be racing with destruction of VMO. As we currently hold our lock we know that our
      // back pointer is correct in so far as the VmCowPages has not yet had completed running its
      // destructor, so we know it is safe to attempt to upgrade it to a RefPtr. If upgrading fails
      // we assume the page is about to be removed from the page queue once the VMO destructor gets
      // a chance to run.
      return VmoBacklink{fbl::MakeRefPtrUpgradeFromRaw(cow, guard), page, page_offset};
    }
  }

  // Ignore any requests to evict from the active queues as this is never allowed.
  lowest_queue = ktl::max(lowest_queue, kNumActiveQueues);
  // The target gen is 1 larger than the lowest queue because evicting from queue X is done by
  // attempting to make the lru queue be X+1.
  return ProcessLruQueue(mru_gen_.load(ktl::memory_order_relaxed) - (lowest_queue - 1), true);
}

fitx::result<zx_status_t, ktl::optional<PageQueues::VmoBacklink>> PageQueues::GetCowWithReplaceablePage(vm_page_t* page, VmCowPages* owning_cow, zx_duration_t unpin_age_threshold) {
  // Wait for the page to not be in a transient state.  This is in a loop, since the wait happens
  // outside the lock, so another thread doing commit/decommit on owning_cow can cause the page
  // state to change, potentially multiple times.
  //
  // While it's possible for another thread that's concurrently committing/decommitting this page
  // to/from owning_cow, or moving the page from one VmCowPages to another without going through
  // FREE, to interfere to some extent with this thread's progress toward a terminal state in this
  // loop (and the caller's loop), this interference is fairly similar to page eviction interfering
  // with progress of commit of a pager-backed range.  That said, we mitigate here by tracking which
  // cases we've seen that we only expect to see once in the absence of commit/decommit interference
  // by another thread.  Thanks to loan_cancelled, we can limit all the wait required cases to a max
  // of once.  This mitigation doesn't try to maximally detect interference and minimize iterations
  // but the mitigation does limit iterations to a finite number.
  while (true) {
    StackLinkableEvent event;
    {  // scope preempt_disable and guard
      Guard<CriticalMutex> guard{&lock_};
      // While holding lock_, we can safely add an event to be notified, if needed.  While a page
      // state transition from ALLOC to OBJECT, and from OBJECT with no VmCowPages to OBJECT with a
      // VmCowPages, are both guarded by lock_, a transition to FREE is not.  So we must check
      // again, in an ordered fashion (using PmmNode lock not just "relaxed" atomic) for the page
      // being in FREE state after we add an event, to ensure the transition to FREE doesn't miss
      // the added event.  If a page transitions back out of FREE due to actions by other threads,
      // the lock_ protects the page's object field from being overwritten by an event being added.
      vm_page_state state = page->state();
      switch (state) {
        case vm_page_state::FREE:
          // No cow, but still success.  The fact that we were holding lock_ while reading page
          // state isn't relevant to the transition to FREE; we just care that we'll notice FREE
          // somewhere in the loop.
          //
          // We care that we will notice transition _to_ FREE that stays FREE indefinitely via this
          // check.  Other threads doing commit/decommit on owning_cow can cause this check to miss
          // a transient FREE state, but we avoid getting stuck waiting indefinitely.
          return fitx::ok(ktl::nullopt);
        case vm_page_state::OBJECT: {
          // Sub-cases:
          //  * Using cow.
          //  * Loaning cow.
          //  * No cow (page moving from cow to cow).
          VmCowPages* cow = reinterpret_cast<VmCowPages*>(page->object.get_object());
          if (!cow) {
            if (!owning_cow) {
              // If there's not a specific owning_cow, then we can't be as certain of the states the
              // page may reach.  For example the page may get used by something other than a
              // VmCowPages, which wouldn't trigger the event.  So we can't use the event mechanism.
              //
              // This is a success case.  We checked if there was a using cow at the moment, and
              // there wasn't.
              return fitx::ok(ktl::nullopt);
            }
            // Page is moving from cow to cow, and/or is on the way to FREE, so set up to get
            // signalled when page gets a new VmCowPages or becomes FREE.  We still have to check
            // for FREE below, since OBJECT to FREE doesn't hold lock_.
            page->object.add_event(&event);
          } else if (cow == owning_cow) {
            DEBUG_ASSERT(owning_cow);
            // Another thread has already put this page in owning_cow, so there is no borrowing
            // cow.  Success.
            return fitx::ok(ktl::nullopt);
          } else {
            // At this point the page may have pin_count != 0.  We have to check in terms of which
            // queue here, since we can't acquire the VmCowPages lock (wrong order).
            if (!owning_cow) {
              if (page->object.get_page_queue_ref().load(fbl::memory_order_relaxed) == PageQueueWired) {
                // A pinned page is not replaceable.
                return fitx::ok(ktl::nullopt);
              } else {
                // The unpin_time is an approximate mechanism to determine if a page _may_ have been
                // unpinned recently; if so, we don't want to replace the page unless the sweep is
                // a non-continuous sweep (like the one that happens when triggered manually or at
                // OOM).
                uint8_t unpin_time = page->object.get_unpin_time();
                uint8_t shifted_unpin_time = unpin_time << (8 - VM_PAGE_OBJECT_UNPIN_TIME_BITS);
                zx_time_t now = current_time();
                uint8_t shifted_now = (now / kUnpinTimeResolutionNanos % VM_PAGE_OBJECT_UNPIN_TIME_MODULUS) << (8 - VM_PAGE_OBJECT_UNPIN_TIME_BITS);
                uint8_t shifted_unpin_age = shifted_now - shifted_unpin_time;
                uint8_t unpin_age_8 = shifted_unpin_age >> (8 - VM_PAGE_OBJECT_UNPIN_TIME_BITS);
                zx_duration_t unpin_age_64 = static_cast<zx_duration_t>(static_cast<uint64_t>(unpin_age_8) * kUnpinTimeResolutionNanos);
                if (unpin_age_64 < unpin_age_threshold) {
                  // This page _appears_ to have _possibly_ been unpinned too recently, so consider
                  // the page to not be replaceable.
                  return fitx::ok(ktl::nullopt);
                }
              }
            }
            // There is a using/borrowing cow, but we may not be able to get a ref to it, if it's
            // already destructing.
            uint64_t page_offset = page->object.get_page_offset();
            // We may be racing with destruction of VMO. As we currently hold our lock we know that
            // our back pointer is correct in so far as the VmCowPages has not yet had completed
            // running its destructor, so we know it is safe to attempt to upgrade it to a RefPtr.
            // If upgrading fails we assume the page's state will become FREE once the VMO
            // destructor gets a chance to run.
            VmoBacklink backlink{fbl::MakeRefPtrUpgradeFromRaw(cow, guard), page, page_offset};
            if (!backlink.cow) {
              // Existing cow is destructing.  The page is becoming FREE.
              if (!owning_cow) {
                // The using cow is actively working on no longer using the page, so the caller
                // doesn't need to do anything further with the page.
                return fitx::ok(ktl::nullopt);
              }
              // When we have an owning_cow, we want to wait for the page.  It's possible another
              // thread doing a commit on owning_cow will take the page from FREE to OBJECT with
              // cow == owning_cow before this thread notices FREE.  Still, we wait for the page
              // state to change to FREE, or back to OBJECT, by adding an event here.
              page->object.add_event(&event);
            } else {
              // We AddRef(ed) the using cow.  Success.  Return the backlink.
              return fitx::ok(backlink);
            }
          }
          break;
        }
        case vm_page_state::ALLOC:
          if (!owning_cow) {
            // When there's not an owning_cow, we don't know what use the page may be put to, so
            // we don't know if an added event would ever get signaled.  Since the caller isn't
            // trying to return pages to a specific owning_cow, the caller is ok with a successful
            // "none" here.
            return fitx::ok(ktl::nullopt);
          }
          // Wait for ALLOC to become OBJECT or FREE.
          page->object.add_event(&event);
          break;
        default:
          // If owning_cow, we know the owning_cow destructor can't run, so the only valid page
          // states while FREE or borrowed by a VmCowPages and not pinned are FREE, ALLOC, OBJECT.
          DEBUG_ASSERT(!owning_cow);
          // When !owning_cow, the possible page states include all page states.  The caller is only
          // interested in pages that are both used by a VmCowPages and which the caller can replace
          // with a different page, so WIRED state goes along with the list of other states where
          // the caller can't just replace the page.  There is no cow with this page as a
          // replaceable page.
          return fitx::ok(ktl::nullopt);
      }
    }  // ~guard, ~preempt_disable
    // If we get here, we know that event was added to the page above.  That never happens when !owning_cow.
    DEBUG_ASSERT(!owning_cow);

    // Before we can wait, we have to fence out the possibility of the page having become FREE just
    // before the event was added above, as transition to FREE is done outside lock_.  It's fine if
    // the page has already moved on from the FREE state such that this check fails, as in that case
    // another thread will take care of signalling the event and we won't get stuck waiting
    // indefinitely.
    //
    // We must acquire the pmm lock for this, because the page state update is "relaxed", but we
    // need to know that this check is sync'ed with the change of page state by pmm to FREE + the
    // associated check for any events to signal under the same lock hold interval.  Without the
    // pmm lock, the "relaxed" state update could be started by the other thread before this thread
    // added the event, yet not be visible to this thread until after a non-locked check for FREE
    // here, causing us to have an event added waiting for FREE that missed the transition to FREE.
    pmm_ensure_signal_if_free(page);

    ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::PAGER);
    zx_status_t wait_result = event.event.Wait(Deadline::infinite());
    // ZX_ERR_TIMED_OUT isn't possible, and failures are never signalled via this event.
    DEBUG_ASSERT(wait_result == ZX_OK || wait_result == ZX_ERR_INTERNAL_INTR_KILLED || wait_result == ZX_ERR_INTERNAL_INTR_RETRY);
    if (wait_result != ZX_OK) {
      RemoveEvent(page, &event);
      return fitx::error(wait_result);
    }
    // At this point, the state of the page has changed, but we don't know how much.  Another thread
    // doing commit on owning_cow may have finished moving the page into owning_cow.  Yet another
    // thread may have decommitted the page again, and yet another thread may be using the loaned
    // page again now despite loan_cancelled having been used.  The page may have been moved to a
    // destination cow, but is now moving again.
    //
    // It's still the goal of this method to return the borrowing cow if there is one, or return
    // success wihtout a borrowing cow if the page is verified to be reclaim-able by the owning_cow
    // at some point during this method (regardless of whether that remains true).
    //
    // Go around again to observe page state.
  }
}

void PageQueues::RemoveEvent(vm_page_t* page, StackLinkableEvent* event) {
  // Lock as required by remove_event.
  {  // scope preempt_disable and guard
    Guard<CriticalMutex> guard{&lock_};
    page->object.remove_event(event);
  }  // ~guard, ~preempt_disable
  // remove_event requires giving pmm a chance to signal in case there are other waiters and the
  // page became FREE while remove_event had the event list stashed locally.  This also acquires the
  // pmm lock which fences out any concurrent (with above remove_event) signal_waiters by pmm, which
  // makes it safe to delete the event even if the event wasn't found by remove_event due to
  // concurrent signal_waiters.
  pmm_ensure_signal_if_free(page);
}
