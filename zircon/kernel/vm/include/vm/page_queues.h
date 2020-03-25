// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_QUEUES_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_QUEUES_H_

#include <zircon/listnode.h>

#include <fbl/macros.h>
#include <kernel/lockdep.h>
#include <kernel/spinlock.h>
#include <vm/page.h>

class VmObjectPaged;

// Allocated pages that are part of a paged VmObject can be placed in a page queue. The page queues
// provide a way to
//  * Classify and group pages across VMO boundaries
//  * Retrieve the VMO that a page is contained in (via a back reference stored in the vm_page_t)
// Once a page has been placed in a page queue its queue_node becomes owned by the page queue and
// must not be used until the page has been Remove'd. It is not sufficient to call list_delete on
// the queue_node yourself as this operation is not atomic and needs to be performed whilst holding
// the PageQueues::lock_.
class PageQueues {
 public:
  PageQueues();
  ~PageQueues();

  DISALLOW_COPY_ASSIGN_AND_MOVE(PageQueues);

  // Place page in the wired queue. Must not already be in a page queue.
  void SetWired(vm_page_t* page);
  // Moves page from whichever queue it is currently in, to the wired queue.
  void MoveToWired(vm_page_t* page);
  // Place page in the unswappable queue. Must not already be in a page queue.
  void SetUnswappable(vm_page_t* page);
  // Moves page from whichever queue it is currently in, to the unswappable queue.
  void MoveToUnswappable(vm_page_t* page);
  // Place page in the pager backed queue. Must not already be in a page queue. Sets the back
  // reference information. If the page is removed from the referenced object (especially if it's
  // due to the object being destroyed) then this back reference *must* be updated, either by
  // calling Remove or calling MoveToPagerBacked with the new object information.
  void SetPagerBacked(vm_page_t* page, VmObjectPaged* object, uint64_t page_offset);
  // Moves page from whichever queue it is currently in, to the pager backed queue. Same rules on
  // keeping the back reference up to date as given in SetPagerBacked apply.
  void MoveToPagerBacked(vm_page_t* page, VmObjectPaged* object, uint64_t page_offset);
  // Removes the page from any page list and returns ownership of the queue_node.
  void Remove(vm_page_t* page);
  // Batched version of Remove that also places all the pages in the specified list
  void RemoveArrayIntoList(vm_page_t** page, size_t count, list_node_t* out_list);

  // Helper struct to group queue length counts returned by DebugQueueCounts.
  struct Counts {
    size_t pager_backed = 0;
    size_t unswappable = 0;
    size_t wired = 0;

    bool operator==(const Counts& other) const {
      return pager_backed == other.pager_backed && unswappable == other.unswappable &&
             wired == other.wired;
    }
    bool operator!=(const Counts& other) const { return !(*this == other); }
  };

  // These functions are marked debug as they perform O(n) traversals of the queues and will hold
  // the lock for the entire time. As such they should only be used for tests or instrumented
  // debugging.
  Counts DebugQueueCounts() const;
  bool DebugPageIsPagerBacked(const vm_page_t* page) const;
  bool DebugPageIsUnswappable(const vm_page_t* page) const;
  bool DebugPageIsWired(const vm_page_t* page) const;

 private:
  DECLARE_SPINLOCK(PageQueues) mutable lock_;
  // pager_backed_ denotes pages that both have a user level pager associated with them, and could
  // be evicted such that the pager could re-create the page.
  list_node_t pager_backed_ TA_GUARDED(lock_) = LIST_INITIAL_CLEARED_VALUE;
  // unswappable_ pages have no user level mechanism to swap/evict them, but are modifiable by the
  // kernel and could have compression etc applied to them.
  list_node_t unswappable_ TA_GUARDED(lock_) = LIST_INITIAL_CLEARED_VALUE;
  // wired pages include kernel data structures or memory pinned for devices and these pages must
  // not be touched in any way, removing both eviction and other strategies such as compression.
  list_node_t wired_ TA_GUARDED(lock_) = LIST_INITIAL_CLEARED_VALUE;

  void RemoveLocked(vm_page_t* page) TA_REQ(lock_);

  bool DebugPageInList(const list_node_t* list, const vm_page_t* page) const;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_QUEUES_H_
