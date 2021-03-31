// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fbl/ref_counted_upgradeable.h>
#include <vm/page_queues.h>
#include <vm/vm_cow_pages.h>

PageQueues::PageQueues() {
  for (list_node& pager_backed : pager_backed_) {
    list_initialize(&pager_backed);
  }
  list_initialize(&pager_backed_inactive_);
  list_initialize(&unswappable_);
  list_initialize(&wired_);
  list_initialize(&unswappable_zero_fork_);
}

PageQueues::~PageQueues() {
  for (list_node& pager_backed : pager_backed_) {
    DEBUG_ASSERT(list_is_empty(&pager_backed));
  }
  DEBUG_ASSERT(list_is_empty(&pager_backed_inactive_));
  DEBUG_ASSERT(list_is_empty(&unswappable_));
  DEBUG_ASSERT(list_is_empty(&wired_));
  DEBUG_ASSERT(list_is_empty(&unswappable_zero_fork_));
  for (size_t i = 0; i < page_queue_counts_.size(); i++) {
    DEBUG_ASSERT_MSG(page_queue_counts_[i] == 0, "i=%zu count=%zd", i, page_queue_counts_[i]);
  }
}

inline size_t PageQueues::rotated_index(size_t index) const {
  DEBUG_ASSERT(index < kNumPagerBacked);
  return (kNumPagerBacked - index + pager_queue_rotation_) & kPagerQueueIndexMask;
}

inline PageQueues::PageQueue PageQueues::GetPagerBackedQueueLocked(size_t index) const {
  return static_cast<PageQueue>(PageQueuePagerBackedBase + rotated_index(index));
}

inline ssize_t& PageQueues::GetPagerBackedQueueCountLocked(size_t index) {
  return page_queue_counts_[GetPagerBackedQueueLocked(index)];
}

inline ssize_t PageQueues::GetPagerBackedQueueCountLocked(size_t index) const {
  return page_queue_counts_[GetPagerBackedQueueLocked(index)];
}

inline list_node_t* PageQueues::GetPagerBackedQueueHeadLocked(size_t index) {
  return &pager_backed_[rotated_index(index)];
}

inline const list_node_t* PageQueues::GetPagerBackedQueueHeadLocked(size_t index) const {
  return &pager_backed_[rotated_index(index)];
}

inline void PageQueues::UpdateCountsLocked(vm_page_t* page, PageQueue destination) {
  DEBUG_ASSERT(page->object.get_page_queue() < PageQueueEntries);
  DEBUG_ASSERT(destination < PageQueueEntries);

  // The counter index stored in vm_page_t is always valid, except for pages that have been in
  // pager-backed queues long enough to be moved during a rotation, in which case the count for the
  // page is migrated to the counter for the current oldest pager-backed queue.
  const bool is_page_queue_valid =
      !is_pager_backed(page->object.get_page_queue()) ||
      pager_queue_rotation_ < page->object.get_pager_queue_merge_rotation();

  const auto source = is_page_queue_valid ? static_cast<PageQueue>(page->object.get_page_queue())
                                          : GetPagerBackedQueueLocked(kOldestIndex);

  page_queue_counts_[source]--;
  page_queue_counts_[destination]++;
  page->object.set_page_queue(destination);

  // Mark the future generation when the page will have been in a pager-backed queue long enough to
  // get moved by a queue rotation if it enters the earliest queue now.
  if (is_pager_backed(destination)) {
    page->object.set_pager_queue_merge_rotation(pager_queue_rotation_ + kNumPagerBacked);
  }
}

void PageQueues::RotatePagerBackedQueues() {
  Guard<CriticalMutex> guard{&lock_};

  // Prepare for rotating the queues by appending the oldest list onto the next oldest list, keeping
  // the overall age ordering and emptying the entry that will become the newest list when the
  // rotation generation is updated.
  list_node_t* oldest = GetPagerBackedQueueHeadLocked(kOldestIndex);
  list_node_t* next_oldest = GetPagerBackedQueueHeadLocked(kOldestIndex - 1);
  list_node_t* next_oldest_end = list_peek_tail(next_oldest);
  list_splice_after(oldest, next_oldest_end ? next_oldest_end : next_oldest);

  // Add the oldest count into the next oldest count and clear the entry that will become the newest
  // count when the rotation generation is updated.
  GetPagerBackedQueueCountLocked(kOldestIndex - 1) += GetPagerBackedQueueCountLocked(kOldestIndex);
  GetPagerBackedQueueCountLocked(kOldestIndex) = 0;

  // Rotate the queues by advancing the rotation generation.
  pager_queue_rotation_++;
}

void PageQueues::SetWired(vm_page_t* page) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  Guard<CriticalMutex> guard{&lock_};
  DEBUG_ASSERT(!list_in_list(&page->queue_node));
  page->object.set_object(nullptr);
  page->object.set_page_offset(0);
  list_add_head(&wired_, &page->queue_node);
  UpdateCountsLocked(page, PageQueueWired);
}

void PageQueues::MoveToWired(vm_page_t* page) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  Guard<CriticalMutex> guard{&lock_};
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  page->object.set_object(nullptr);
  page->object.set_page_offset(0);
  list_delete(&page->queue_node);
  list_add_head(&wired_, &page->queue_node);
  UpdateCountsLocked(page, PageQueueWired);
}

void PageQueues::SetUnswappable(vm_page_t* page) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->object.pin_count == 0);
  Guard<CriticalMutex> guard{&lock_};
  DEBUG_ASSERT(!list_in_list(&page->queue_node));
  page->object.set_object(nullptr);
  page->object.set_page_offset(0);
  list_add_head(&unswappable_, &page->queue_node);
  UpdateCountsLocked(page, PageQueueUnswappable);
}

void PageQueues::MoveToUnswappableLocked(vm_page_t* page) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->object.pin_count == 0);
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  page->object.set_object(nullptr);
  page->object.set_page_offset(0);
  list_delete(&page->queue_node);
  list_add_head(&unswappable_, &page->queue_node);
  UpdateCountsLocked(page, PageQueueUnswappable);
}

void PageQueues::MoveToUnswappable(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToUnswappableLocked(page);
}

void PageQueues::SetPagerBacked(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->object.pin_count == 0);
  DEBUG_ASSERT(object);
  Guard<CriticalMutex> guard{&lock_};
  DEBUG_ASSERT(!list_in_list(&page->queue_node));
  page->object.set_object(object);
  page->object.set_page_offset(page_offset);
  list_add_head(GetPagerBackedQueueHeadLocked(kNewestIndex), &page->queue_node);
  UpdateCountsLocked(page, GetPagerBackedQueueLocked(kNewestIndex));
}

void PageQueues::MoveToPagerBacked(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->object.pin_count == 0);
  DEBUG_ASSERT(object);
  Guard<CriticalMutex> guard{&lock_};
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  page->object.set_object(object);
  page->object.set_page_offset(page_offset);
  list_delete(&page->queue_node);
  list_add_head(GetPagerBackedQueueHeadLocked(kNewestIndex), &page->queue_node);
  UpdateCountsLocked(page, GetPagerBackedQueueLocked(kNewestIndex));
}

void PageQueues::MoveToPagerBackedInactive(vm_page_t* page) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->object.pin_count == 0);
  DEBUG_ASSERT(page->object.get_object());
  Guard<CriticalMutex> guard{&lock_};
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  list_delete(&page->queue_node);
  list_add_head(&pager_backed_inactive_, &page->queue_node);
  UpdateCountsLocked(page, PageQueuePagerBackedInactive);
}

void PageQueues::SetUnswappableZeroFork(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->object.pin_count == 0);
  Guard<CriticalMutex> guard{&lock_};
  DEBUG_ASSERT(!list_in_list(&page->queue_node));
  page->object.set_object(object);
  page->object.set_page_offset(page_offset);
  list_add_head(&unswappable_zero_fork_, &page->queue_node);
  UpdateCountsLocked(page, PageQueueUnswappableZeroFork);
}

void PageQueues::MoveToUnswappableZeroFork(vm_page_t* page, VmCowPages* object,
                                           uint64_t page_offset) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->object.pin_count == 0);
  Guard<CriticalMutex> guard{&lock_};
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  page->object.set_object(object);
  page->object.set_page_offset(page_offset);
  list_delete(&page->queue_node);
  list_add_head(&unswappable_zero_fork_, &page->queue_node);
  UpdateCountsLocked(page, PageQueueUnswappableZeroFork);
}

void PageQueues::RemoveLocked(vm_page_t* page) {
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  page->object.set_object(nullptr);
  page->object.set_page_offset(0);
  list_delete(&page->queue_node);
  UpdateCountsLocked(page, PageQueueNone);
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
  Guard<CriticalMutex> guard{&lock_};
  counts.newest = GetPagerBackedQueueCountLocked(kNewestIndex);
  counts.total = counts.newest;
  for (size_t i = 1; i < kOldestIndex; i++) {
    counts.total += GetPagerBackedQueueCountLocked(i);
  }
  counts.oldest = GetPagerBackedQueueCountLocked(kOldestIndex);
  // Account the inactive queue length under |oldest|, since (inactive + oldest LRU) pages are
  // eligible for reclamation first. |oldest| is meant to track pages eligible for eviction first.
  counts.oldest += page_queue_counts_[PageQueuePagerBackedInactive];
  counts.total += counts.oldest;
  return counts;
}

PageQueues::Counts PageQueues::DebugQueueCounts() const {
  Counts counts;
  Guard<CriticalMutex> guard{&lock_};
  for (size_t i = 0; i < kNumPagerBacked; i++) {
    counts.pager_backed[i] = GetPagerBackedQueueCountLocked(i);
  }
  counts.pager_backed_inactive = page_queue_counts_[PageQueuePagerBackedInactive];
  counts.unswappable = page_queue_counts_[PageQueueUnswappable];
  counts.wired = page_queue_counts_[PageQueueWired];
  counts.unswappable_zero_fork = page_queue_counts_[PageQueueUnswappableZeroFork];
  return counts;
}

bool PageQueues::DebugPageInListLocked(const list_node_t* list, const vm_page_t* page) const {
  const vm_page_t* p;
  list_for_every_entry (list, p, vm_page_t, queue_node) {
    if (p == page) {
      return true;
    }
  }
  return false;
}

bool PageQueues::DebugPageInList(const list_node_t* list, const vm_page_t* page) const {
  Guard<CriticalMutex> guard{&lock_};
  return DebugPageInListLocked(list, page);
}

bool PageQueues::DebugPageIsPagerBacked(const vm_page_t* page, size_t* queue) const {
  Guard<CriticalMutex> guard{&lock_};
  for (size_t i = 0; i < kNumPagerBacked; i++) {
    if (DebugPageInListLocked(GetPagerBackedQueueHeadLocked(i), page)) {
      if (queue) {
        *queue = i;
      }
      return true;
    }
  }
  return false;
}

bool PageQueues::DebugPageIsPagerBackedInactive(const vm_page_t* page) const {
  return DebugPageInList(&pager_backed_inactive_, page);
}

bool PageQueues::DebugPageIsUnswappable(const vm_page_t* page) const {
  return DebugPageInList(&unswappable_, page);
}

bool PageQueues::DebugPageIsWired(const vm_page_t* page) const {
  return DebugPageInList(&wired_, page);
}

bool PageQueues::DebugPageIsUnswappableZeroFork(const vm_page_t* page) const {
  return DebugPageInList(&unswappable_zero_fork_, page);
}

bool PageQueues::DebugPageIsAnyUnswappable(const vm_page_t* page) const {
  Guard<CriticalMutex> guard{&lock_};
  return DebugPageInListLocked(&unswappable_, page) ||
         DebugPageInListLocked(&unswappable_zero_fork_, page);
}

ktl::optional<PageQueues::VmoBacklink> PageQueues::PopUnswappableZeroFork() {
  Guard<CriticalMutex> guard{&lock_};

  vm_page_t* page = list_peek_tail_type(&unswappable_zero_fork_, vm_page_t, queue_node);
  if (!page) {
    return ktl::nullopt;
  }

  VmCowPages* cow = reinterpret_cast<VmCowPages*>(page->object.get_object());
  uint64_t page_offset = page->object.get_page_offset();
  DEBUG_ASSERT(cow);

  page->object.set_object(0);
  page->object.set_page_offset(0);

  list_delete(&page->queue_node);
  list_add_head(&unswappable_, &page->queue_node);
  UpdateCountsLocked(page, PageQueueUnswappable);

  // We may be racing with destruction of VMO. As we currently hold our lock we know that our
  // back pointer is correct in so far as the VmCowPages has not yet had completed running its
  // destructor, so we know it is safe to attempt to upgrade it to a RefPtr. If upgrading fails
  // we assume the page is about to be removed from the page queue once the VMO destructor gets
  // a chance to run.
  return VmoBacklink{fbl::MakeRefPtrUpgradeFromRaw(cow, guard), page, page_offset};
}

ktl::optional<PageQueues::VmoBacklink> PageQueues::PeekPagerBacked(size_t lowest_queue) const {
  Guard<CriticalMutex> guard{&lock_};
  // Peek the tail of the inactive queue first.
  vm_page_t* page = list_peek_tail_type(&pager_backed_inactive_, vm_page_t, queue_node);
  // If a page is not found in the inactive queue, move on to the last LRU queue.
  for (size_t i = kNumPagerBacked; i > lowest_queue && !page; i--) {
    page = list_peek_tail_type(GetPagerBackedQueueHeadLocked(i - 1), vm_page_t, queue_node);
  }
  if (!page) {
    return ktl::nullopt;
  }

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
