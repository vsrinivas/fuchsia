// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fbl/ref_counted_upgradeable.h>
#include <vm/page_queues.h>
#include <vm/vm_cow_pages.h>

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
  VM_KTRACE_DURATION(2, "RotatePagerBackedQueues");
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
    VM_KTRACE_DURATION(2, "ProcessLruQueue");
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
  page->object.set_object(object);
  page->object.set_page_offset(page_offset);
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
  page->object.set_object(object);
  page->object.set_page_offset(page_offset);
  list_delete(&page->queue_node);
  list_add_head(&page_queues_[queue], &page->queue_node);
  page_queue_counts_[old_queue].fetch_sub(1, ktl::memory_order_relaxed);
  page_queue_counts_[queue].fetch_add(1, ktl::memory_order_relaxed);
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
