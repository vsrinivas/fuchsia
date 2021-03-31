// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_QUEUES_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_QUEUES_H_

#include <sys/types.h>
#include <zircon/listnode.h>

#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <kernel/lockdep.h>
#include <kernel/mutex.h>
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

  static_assert(fbl::is_pow2(kNumPagerBacked), "kNumPagerBacked must be a power of 2!");
  static_assert(kNumPagerBacked > 2, "kNumPagerBacked must be greater than 2!");

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
  // Moves page from whichever queue it is currently in, to the inactive pager backed queue. The
  // object back reference information must have already been set by a previous call to
  // SetPagerBacked or MoveToPagerBacked. Same rules on keeping the back reference up to date as
  // given in SetPagerBacked apply.
  void MoveToPagerBackedInactive(vm_page_t* page);
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
  // highly discouraged as the underlying lock is a CriticalMutex which disables preemption.
  // Preferably *Array variations should be used, but this provides a higher performance mechanism
  // when needed.
  Lock<CriticalMutex>* get_lock() TA_RET_CAP(lock_) { return &lock_; }

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
    size_t pager_backed_inactive = 0;
    size_t unswappable = 0;
    size_t wired = 0;
    size_t unswappable_zero_fork = 0;

    bool operator==(const Counts& other) const {
      return pager_backed == other.pager_backed &&
             pager_backed_inactive == other.pager_backed_inactive &&
             unswappable == other.unswappable && wired == other.wired &&
             unswappable_zero_fork == other.unswappable_zero_fork;
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
  bool DebugPageIsPagerBackedInactive(const vm_page_t* page) const;
  bool DebugPageIsUnswappable(const vm_page_t* page) const;
  bool DebugPageIsUnswappableZeroFork(const vm_page_t* page) const;
  bool DebugPageIsAnyUnswappable(const vm_page_t* page) const;
  bool DebugPageIsWired(const vm_page_t* page) const;

 private:
  static constexpr size_t kNewestIndex = 0;
  static constexpr size_t kOldestIndex = kNumPagerBacked - 1;
  static constexpr size_t kPagerQueueIndexMask = kNumPagerBacked - 1;

  // Specifies the indices of the page queue counters.
  enum PageQueue : uint8_t {
    PageQueueNone = 0,
    PageQueueUnswappable,
    PageQueueWired,
    PageQueueUnswappableZeroFork,
    PageQueuePagerBackedInactive,
    PageQueuePagerBackedBase,
    PageQueueEntries = PageQueuePagerBackedBase + kNumPagerBacked,
  };

  // Ensure that the pager-backed queue counts are always at the end.
  static_assert(PageQueuePagerBackedBase + kNumPagerBacked == PageQueueEntries);

  static constexpr bool is_pager_backed(PageQueue value) {
    return value >= PageQueuePagerBackedBase;
  }
  static constexpr bool is_pager_backed(uint8_t value) {
    return is_pager_backed(static_cast<PageQueue>(value));
  }

  // Returns the pager queue index adjusted for the current rotation.
  inline size_t rotated_index(size_t index) const TA_REQ(lock_);

  // Returns the PageQueue index for the pager queue index adjusted for the current rotation.
  inline PageQueue GetPagerBackedQueueLocked(size_t index) const TA_REQ(lock_);

  // Returns the list node for the pager queue index adjusted for the current rotation.
  inline list_node_t* GetPagerBackedQueueHeadLocked(size_t index) TA_REQ(lock_);
  inline const list_node_t* GetPagerBackedQueueHeadLocked(size_t index) const TA_REQ(lock_);

  // Returns a reference to the page count for the pager queue adjusted for the current rotation.
  inline ssize_t& GetPagerBackedQueueCountLocked(size_t index) TA_REQ(lock_);
  inline ssize_t GetPagerBackedQueueCountLocked(size_t index) const TA_REQ(lock_);

  // Updates the source and destination counters of the given page and records the destination queue
  // in the page.
  inline void UpdateCountsLocked(vm_page_t* page, PageQueue destination) TA_REQ(lock_);

  DECLARE_CRITICAL_MUTEX(PageQueues) mutable lock_;
  // pager_backed_ denotes pages that both have a user level pager associated with them, and could
  // be evicted such that the pager could re-create the page.
  //
  // Pages in these queues are periodically aged by circularly rotating which entries represent the
  // newest, intermediate, and oldest pages. When performing a rotation, the list in the current
  // oldest entry is appended to the next oldest list, preserving the chronological order of the
  // pages and emptying the list that will become the new earliest list.
  //
  //    Oldest        Newest       Newest  Oldest                Newest  Oldest
  //         |        |                 |  |                          |  |
  //         |        |                 |  |                          |  V
  //         |        |                 |  V                          |  a
  //         V        V    Rotation     V  a          Rotation        V  b
  //        [a][b][c][d]  ---------->  [ ][b][c][d]  ---------->  [e][ ][c][d]
  //
  list_node_t pager_backed_[kNumPagerBacked] TA_GUARDED(lock_) = {LIST_INITIAL_CLEARED_VALUE};
  // tracks pager backed pages that are inactive, kept separate from pager_backed_ to opt out of
  // page queue rotations. Pages are moved into this queue explicitly when they need to be marked
  // inactive, and moved out to pager_backed_[0] on a subsequent access, or evicted under memory
  // pressure before the last pager_backed_ queue.
  list_node_t pager_backed_inactive_ TA_GUARDED(lock_) = LIST_INITIAL_CLEARED_VALUE;
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

  // Offset to apply to the pager-backed queues and corresponding subset of the page count array
  // when rotating pager-backed queues. Rotation happens once every 10 seconds, resulting integer
  // overflow in about 1,360 years.
  uint32_t pager_queue_rotation_ TA_GUARDED(lock_) = 0;

  // Tracks the counts of pages in each queue in O(1) time complexity. As pages are moved between
  // queues, the corresponding source and destination counts are decremented and incremented,
  // respectively.
  //
  // The first entry of the array is special: it logically represents pages not in any queue.
  // For simplicity, it is initialized to zero rather than the total number of pages in the system.
  // Consequently, the value of this entry is a negative number with absolute value equal to the
  // total number of pages in all queues. This approach avoids unnecessary branches when updating
  // counts.
  ktl::array<ssize_t, PageQueueEntries> page_queue_counts_ TA_GUARDED(lock_) = {};

  void RemoveLocked(vm_page_t* page) TA_REQ(lock_);

  bool DebugPageInList(const list_node_t* list, const vm_page_t* page) const;
  bool DebugPageInListLocked(const list_node_t* list, const vm_page_t* page) const TA_REQ(lock_);
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_QUEUES_H_
