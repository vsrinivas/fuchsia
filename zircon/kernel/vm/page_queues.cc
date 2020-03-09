// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fbl/ref_counted_upgradeable.h>
#include <vm/page_queues.h>
#include <vm/vm_object_paged.h>

PageQueues::PageQueues() {
  list_initialize(&pager_backed_);
  list_initialize(&unswappable_);
  list_initialize(&wired_);
}

PageQueues::~PageQueues() {
  DEBUG_ASSERT(list_is_empty(&pager_backed_));
  DEBUG_ASSERT(list_is_empty(&unswappable_));
  DEBUG_ASSERT(list_is_empty(&wired_));
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
  list_add_head(&pager_backed_, &page->queue_node);
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
  list_add_head(&pager_backed_, &page->queue_node);
}

void PageQueues::Remove(vm_page_t* page) {
  Guard<SpinLock, IrqSave> guard{&lock_};
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  page->object.set_object(nullptr);
  page->object.set_page_offset(0);
  list_delete(&page->queue_node);
}

PageQueues::Counts PageQueues::DebugQueueCounts() const {
  Counts counts;
  Guard<SpinLock, IrqSave> guard{&lock_};
  counts.pager_backed = list_length(&pager_backed_);
  counts.unswappable = list_length(&unswappable_);
  counts.wired = list_length(&wired_);
  return counts;
}

bool PageQueues::DebugPageInList(const list_node_t* list, const vm_page_t* page) const {
  const vm_page_t* p;
  Guard<SpinLock, IrqSave> guard{&lock_};
  list_for_every_entry (list, p, vm_page_t, queue_node) {
    if (p == page) {
      return true;
    }
  }
  return false;
}

bool PageQueues::DebugPageIsPagerBacked(const vm_page_t* page) const {
  return DebugPageInList(&pager_backed_, page);
}

bool PageQueues::DebugPageIsUnswappable(const vm_page_t* page) const {
  return DebugPageInList(&unswappable_, page);
}

bool PageQueues::DebugPageIsWired(const vm_page_t* page) const {
  return DebugPageInList(&wired_, page);
}
