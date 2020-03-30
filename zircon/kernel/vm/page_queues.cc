// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fbl/ref_counted_upgradeable.h>
#include <vm/page_queues.h>
#include <vm/vm_object_paged.h>

PageQueues::PageQueues() {
  for (size_t i = 0; i < kNumPagerBacked; i++) {
    list_initialize(&pager_backed_[i]);
  }
  list_initialize(&unswappable_);
  list_initialize(&wired_);
}

PageQueues::~PageQueues() {
  for (size_t i = 0; i < kNumPagerBacked; i++) {
    DEBUG_ASSERT(list_is_empty(&pager_backed_[i]));
  }
  DEBUG_ASSERT(list_is_empty(&unswappable_));
  DEBUG_ASSERT(list_is_empty(&wired_));
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

void PageQueues::MoveToUnswappable(vm_page_t* page) {
  DEBUG_ASSERT(page->state() == VM_PAGE_STATE_OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->object.pin_count == 0);
  Guard<SpinLock, IrqSave> guard{&lock_};
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  page->object.set_object(nullptr);
  page->object.set_page_offset(0);
  list_delete(&page->queue_node);
  list_add_head(&unswappable_, &page->queue_node);
}

void PageQueues::SetPagerBacked(vm_page_t* page, VmObjectPaged* object, uint64_t page_offset) {
  DEBUG_ASSERT(page->state() == VM_PAGE_STATE_OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->object.pin_count == 0);
  DEBUG_ASSERT(object);
  Guard<SpinLock, IrqSave> guard{&lock_};
  DEBUG_ASSERT(!list_in_list(&page->queue_node));
  page->object.set_object(reinterpret_cast<void*>(object));
  page->object.set_page_offset(page_offset);
  list_add_head(&pager_backed_[0], &page->queue_node);
}

void PageQueues::MoveToPagerBacked(vm_page_t* page, VmObjectPaged* object, uint64_t page_offset) {
  DEBUG_ASSERT(page->state() == VM_PAGE_STATE_OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(page->object.pin_count == 0);
  DEBUG_ASSERT(object);
  Guard<SpinLock, IrqSave> guard{&lock_};
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  page->object.set_object(reinterpret_cast<void*>(object));
  page->object.set_page_offset(page_offset);
  list_delete(&page->queue_node);
  list_add_head(&pager_backed_[0], &page->queue_node);
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

PageQueues::Counts PageQueues::DebugQueueCounts() const {
  Counts counts;
  Guard<SpinLock, IrqSave> guard{&lock_};
  for (size_t i = 0; i < kNumPagerBacked; i++) {
    counts.pager_backed[i] = list_length(&pager_backed_[i]);
  }
  counts.unswappable = list_length(&unswappable_);
  counts.wired = list_length(&wired_);
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
