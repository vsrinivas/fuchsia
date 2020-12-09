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
#include <ktl/array.h>
#include <ktl/optional.h>
#include <vm/page.h>

class VmCowPages;

// Allocated pages that are part of the cow pages in a VmObjectPaged can be placed in a page queue.
// The page queues provide a way to
//  * Classify and group pages across VMO boundaries
//  * Retrieve the VMO that a page is contained in (via a back reference stored in the vm_page_t)
// Once a page has been placed in a page queue its queue_node becomes owned by the page queue and
// must not be used until the page has been Remove'd. It is not sufficient to call list_delete on
// the queue_node yourself as this operation is not atomic and needs to be performed whilst holding
// the PageQueues::lock_.
class PageQueues {
 public:
  // The number of pager backed queues is slightly arbitrary, but to be useful you want at least 3
  // representing
  //  * Very new pages that you probably don't want to evict as doing so probably implies you are in
  //    swap death
  //  * Slightly old pages that could be evicted if needed
  //  * Very old pages that you'd be happy to evict
  // For now 4 queues are chosen to stretch out that middle group such that the distinction between
  // slightly old and very old is more pronounced.
  static constexpr size_t kNumPagerBacked = 4;

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
  void SetPagerBacked(vm_page_t* page, VmCowPages* object, uint64_t page_offset);
  // Moves page from whichever queue it is currently in, to the pager backed queue. Same rules on
  // keeping the back reference up to date as given in SetPagerBacked apply.
  void MoveToPagerBacked(vm_page_t* page, VmCowPages* object, uint64_t page_offset);
  // Moves page from whichever queue it is currently in, to the back of the pager backed queue (in
  // contrast MoveToPagerBacked moves to the front of the queue). Same rules on keeping the back
  // reference up to date as given in SetPagerBacked apply.
  void MoveToEndOfPagerBacked(vm_page_t* page);
  // Place page in the unswappable zero forked queue. Must not already be in a page queue. Same
  // rules for back pointers apply as for SetPagerBacked.
  void SetUnswappableZeroFork(vm_page_t* page, VmCowPages* object, uint64_t page_offset);
  // Moves page from whichever queue it is currently in, to the unswappable zero forked queue. Same
  // rules for back pointers apply as for SetPagerBacked.
  void MoveToUnswappableZeroFork(vm_page_t* page, VmCowPages* object, uint64_t page_offset);

  // Removes the page from any page list and returns ownership of the queue_node.
  void Remove(vm_page_t* page);
  // Batched version of Remove that also places all the pages in the specified list
  void RemoveArrayIntoList(vm_page_t** page, size_t count, list_node_t* out_list);

  // Variation on MoveToUnswappable that allows for already holding the lock.
  void MoveToUnswappableLocked(vm_page_t* page) TA_REQ(lock_);

  // Provides access to the underlying lock, allowing _Locked variants to be called. Use of this is
  // highly discouraged as the underlying lock is a spinlock, which cannot generally be held safely,
  // specifically it is unsafe to access the heap whilst holding this lock. Preferably *Array
  // variations should be used, but this provides a higher performance mechanism when needed.
  Lock<SpinLock>* get_lock() TA_RET_CAP(lock_) { return &lock_; }

  // Rotates the pager backed queues such that all the pages in queue J get moved to queue J+1.
  // This leaves queue 0 empty and the last queue (kNumPagerBacked - 1) has both its old contents
  // and gains the contents of the queue before it.
  // That is given 4 queues each with one page:
  // {[a], [b], [c], [d]}
  // After rotation they will be
  // {[], [a], [b], [d,c]}
  void RotatePagerBackedQueues();

  // Used to represent and return page backlink information acquired whilst holding the page queue
  // lock. The contained vmo could be null if the refptr could not be upgraded, indicating that the
  // vmo was being destroyed whilst trying to construct the backlink.
  // The page and offset contained here are not synchronized and must be separately validated before
  // use. This can be done by acquiring the returned vmo's lock and then validating that the page is
  // still contained at the offset.
  struct VmoBacklink {
    VmoBacklink() = default;

    fbl::RefPtr<VmCowPages> cow;
    vm_page_t* page = nullptr;
    uint64_t offset = 0;
  };

  // Moves a page from from the unswappable zero fork queue into the unswappable queue and returns
  // the backlink information. If the zero fork queue is empty then a nullopt is returned, otherwise
  // if it has_value the vmo field may be null to indicate that the vmo is running its destructor
  // (see VmoBacklink for more details).
  ktl::optional<VmoBacklink> PopUnswappableZeroFork();

  // Looks at the pager_backed queues from highest down to |lowest_queue| and returns backlink
  // information of the first page found. If no page was found a nullopt is returned, otherwise if
  // it has_value the vmo field may be null to indicate that the vmo is running its destructor (see
  // VmoBacklink for more details). If a page is returned its location in the pager_backed queue is
  // not modified.
  ktl::optional<VmoBacklink> PeekPagerBacked(size_t lowest_queue) const;

  // Helper struct to group pager-backed queue length counts returned by GetPagerQueueCounts.
  struct PagerCounts {
    size_t total = 0;
    size_t newest = 0;
    size_t oldest = 0;
  };

  // Returns pager-backed queue counts. Called from the zx_object_get_info() syscall.
  // Performs O(n) traversal of the pager-backed queues.
  PagerCounts GetPagerQueueCounts() const;

  // Helper struct to group queue length counts returned by DebugQueueCounts.
  struct Counts {
    ktl::array<size_t, kNumPagerBacked> pager_backed = {0};
    size_t unswappable = 0;
    size_t wired = 0;
    size_t unswappable_zero_fork = 0;

    bool operator==(const Counts& other) const {
      return pager_backed == other.pager_backed && unswappable == other.unswappable &&
             wired == other.wired && unswappable_zero_fork == other.unswappable_zero_fork;
    }
    bool operator!=(const Counts& other) const { return !(*this == other); }
  };

  // These functions are marked debug as they perform O(n) traversals of the queues and will hold
  // the lock for the entire time. As such they should only be used for tests or instrumented
  // debugging.
  Counts DebugQueueCounts() const;
  // This takes an optional output parameter that, if the function returns true, will contain the
  // index of the queue that the page was in.
  bool DebugPageIsPagerBacked(const vm_page_t* page, size_t* queue = nullptr) const;
  bool DebugPageIsUnswappable(const vm_page_t* page) const;
  bool DebugPageIsUnswappableZeroFork(const vm_page_t* page) const;
  bool DebugPageIsAnyUnswappable(const vm_page_t* page) const;
  bool DebugPageIsWired(const vm_page_t* page) const;

 private:
  DECLARE_SPINLOCK(PageQueues) mutable lock_;
  // pager_backed_ denotes pages that both have a user level pager associated with them, and could
  // be evicted such that the pager could re-create the page.
  list_node_t pager_backed_[kNumPagerBacked] TA_GUARDED(lock_) = {LIST_INITIAL_CLEARED_VALUE};
  // unswappable_ pages have no user level mechanism to swap/evict them, but are modifiable by the
  // kernel and could have compression etc applied to them.
  list_node_t unswappable_ TA_GUARDED(lock_) = LIST_INITIAL_CLEARED_VALUE;
  // wired pages include kernel data structures or memory pinned for devices and these pages must
  // not be touched in any way, removing both eviction and other strategies such as compression.
  list_node_t wired_ TA_GUARDED(lock_) = LIST_INITIAL_CLEARED_VALUE;
  // these are a subset of the unswappable_ pages that were forked from the zero pages. Pages being
  // in this list is purely a hint, and it is correct for pages to at any point be moved between the
  // unswappable_ and unswappabe_zero_fork_ lists.
  list_node_t unswappable_zero_fork_ TA_GUARDED(lock_) = LIST_INITIAL_CLEARED_VALUE;

  void RemoveLocked(vm_page_t* page) TA_REQ(lock_);

  bool DebugPageInList(const list_node_t* list, const vm_page_t* page) const;
  bool DebugPageInListLocked(const list_node_t* list, const vm_page_t* page) const TA_REQ(lock_);
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_QUEUES_H_
