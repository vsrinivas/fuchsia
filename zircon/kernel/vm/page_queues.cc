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
  list_initialize(&unswappable_);
  list_initialize(&wired_);
  list_initialize(&unswappable_zero_fork_);
}

PageQueues::~PageQueues() {
  for (list_node& pager_backed : pager_backed_) {
    DEBUG_ASSERT(list_is_empty(&pager_backed));
  }
  DEBUG_ASSERT(list_is_empty(&unswappable_));
  DEBUG_ASSERT(list_is_empty(&wired_));
  DEBUG_ASSERT(list_is_empty(&unswappable_zero_fork_));
}

void PageQueues::RotatePagerBackedQueues() {
  Guard<SpinLock, IrqSave> guard{&lock_};
  for (size_t i = kNumPagerBacked - 1; i > 0; i--) {
    list_splice_after(&pager_backed_[i - 1], &pager_backed_[i]);
  }
}

void PageQueues::SetWired(vm_page_t* page) {
  DEBUG_ASSERT(page->state() == VM_PAGE_STATE_OBJECT);
  DEBUG_ASSERT(!page->is_free());
  Guard<SpinLock, IrqSave> guard{&lock_};
  DEBUG_ASSERT(!list_in_list(&page->queue_node));
  page->object.set_object(nullptr);
  page->object.set_page_offset(0);
  list_add_head(&wired_, &page->queue_node);
}

void PageQueues::MoveToWired(vm_page_t* page) {
  DEBUG_ASSERT(page->state() == VM_PAGE_STATE_OBJECT);
  DEBUG_ASSERT(!page->is_free());
  Guard<SpinLock, IrqSave> guard{&lock_};
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  page->object.set_object(nullptr);
  page->object.set_page_offset(0);
  list_delete(&page->queue_node);
  list_add_head(&wired_, &page->queue_node);
}

void PageQueues::SetUnswappable(vm_page_t* page) {
  DEBUG_ASSERT(page->state() == VM_PAGE_STATE_OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->object.pin_count == 0);
  Guard<SpinLock, IrqSave> guard{&lock_};
  DEBUG_ASSERT(!list_in_list(&page->queue_node));
  page->object.set_object(nullptr);
  page->object.set_page_offset(0);
  list_add_head(&unswappable_, &page->queue_node);
}

void PageQueues::MoveToUnswappableLocked(vm_page_t* page) {
  DEBUG_ASSERT(page->state() == VM_PAGE_STATE_OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->object.pin_count == 0);
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  page->object.set_object(nullptr);
  page->object.set_page_offset(0);
  list_delete(&page->queue_node);
  list_add_head(&unswappable_, &page->queue_node);
}

void PageQueues::MoveToUnswappable(vm_page_t* page) {
  Guard<SpinLock, IrqSave> guard{&lock_};
  MoveToUnswappableLocked(page);
}

void PageQueues::SetPagerBacked(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  DEBUG_ASSERT(page->state() == VM_PAGE_STATE_OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->object.pin_count == 0);
  DEBUG_ASSERT(object);
  Guard<SpinLock, IrqSave> guard{&lock_};
  DEBUG_ASSERT(!list_in_list(&page->queue_node));
  page->object.set_object(object);
  page->object.set_page_offset(page_offset);
  list_add_head(&pager_backed_[0], &page->queue_node);
}

void PageQueues::MoveToPagerBacked(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  DEBUG_ASSERT(page->state() == VM_PAGE_STATE_OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->object.pin_count == 0);
  DEBUG_ASSERT(object);
  Guard<SpinLock, IrqSave> guard{&lock_};
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  page->object.set_object(object);
  page->object.set_page_offset(page_offset);
  list_delete(&page->queue_node);
  list_add_head(&pager_backed_[0], &page->queue_node);
}

void PageQueues::SetUnswappableZeroFork(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  DEBUG_ASSERT(page->state() == VM_PAGE_STATE_OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->object.pin_count == 0);
  Guard<SpinLock, IrqSave> guard{&lock_};
  DEBUG_ASSERT(!list_in_list(&page->queue_node));
  page->object.set_object(object);
  page->object.set_page_offset(page_offset);
  list_add_head(&unswappable_zero_fork_, &page->queue_node);
}

void PageQueues::MoveToUnswappableZeroFork(vm_page_t* page, VmCowPages* object,
                                           uint64_t page_offset) {
  DEBUG_ASSERT(page->state() == VM_PAGE_STATE_OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->object.pin_count == 0);
  Guard<SpinLock, IrqSave> guard{&lock_};
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  page->object.set_object(object);
  page->object.set_page_offset(page_offset);
  list_delete(&page->queue_node);
  list_add_head(&unswappable_zero_fork_, &page->queue_node);
}

void PageQueues::RemoveLocked(vm_page_t* page) {
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  page->object.set_object(nullptr);
  page->object.set_page_offset(0);
  list_delete(&page->queue_node);
}

void PageQueues::Remove(vm_page_t* page) {
  Guard<SpinLock, IrqSave> guard{&lock_};
  RemoveLocked(page);
}

void PageQueues::RemoveArrayIntoList(vm_page_t** pages, size_t count, list_node_t* out_list) {
  DEBUG_ASSERT(pages);
  Guard<SpinLock, IrqSave> guard{&lock_};
  for (size_t i = 0; i < count; i++) {
    DEBUG_ASSERT(pages[i]);
    RemoveLocked(pages[i]);
    list_add_tail(out_list, &pages[i]->queue_node);
  }
}

PageQueues::PagerCounts PageQueues::GetPagerQueueCounts() const {
  PagerCounts counts;
  Guard<SpinLock, IrqSave> guard{&lock_};
  counts.newest = list_length(&pager_backed_[0]);
  counts.total = counts.newest;
  for (size_t i = 1; i < kNumPagerBacked - 1; i++) {
    counts.total += list_length(&pager_backed_[i]);
  }
  counts.oldest = list_length(&pager_backed_[kNumPagerBacked - 1]);
  counts.total += counts.oldest;
  return counts;
}

PageQueues::Counts PageQueues::DebugQueueCounts() const {
  Counts counts;
  Guard<SpinLock, IrqSave> guard{&lock_};
  for (size_t i = 0; i < kNumPagerBacked; i++) {
    counts.pager_backed[i] = list_length(&pager_backed_[i]);
  }
  counts.unswappable = list_length(&unswappable_);
  counts.wired = list_length(&wired_);
  counts.unswappable_zero_fork = list_length(&unswappable_zero_fork_);
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
  Guard<SpinLock, IrqSave> guard{&lock_};
  return DebugPageInListLocked(list, page);
}

bool PageQueues::DebugPageIsPagerBacked(const vm_page_t* page, size_t* queue) const {
  Guard<SpinLock, IrqSave> guard{&lock_};
  for (size_t i = 0; i < kNumPagerBacked; i++) {
    if (DebugPageInListLocked(&pager_backed_[i], page)) {
      if (queue) {
        *queue = i;
      }
      return true;
    }
  }
  return false;
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
  Guard<SpinLock, IrqSave> guard{&lock_};
  return DebugPageInListLocked(&unswappable_, page) ||
         DebugPageInListLocked(&unswappable_zero_fork_, page);
}

ktl::optional<PageQueues::VmoBacklink> PageQueues::PopUnswappableZeroFork() {
  Guard<SpinLock, IrqSave> guard{&lock_};
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

  // We may be racing with destruction of VMO. As we currently hold our lock we know that our
  // back pointer is correct in so far as the VmCowPages has not yet had completed running its
  // destructor, so we know it is safe to attempt to upgrade it to a RefPtr. If upgrading fails
  // we assume the page is about to be removed from the page queue once the VMO destructor gets
  // a chance to run.
  return VmoBacklink{fbl::MakeRefPtrUpgradeFromRaw(cow, guard), page, page_offset};
}

ktl::optional<PageQueues::VmoBacklink> PageQueues::PeekPagerBacked(size_t lowest_queue) const {
  Guard<SpinLock, IrqSave> guard{&lock_};
  vm_page_t* page = nullptr;
  for (size_t i = kNumPagerBacked; i > lowest_queue; i--) {
    page = list_peek_tail_type(&pager_backed_[i - 1], vm_page_t, queue_node);
    if (page) {
      break;
    }
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
