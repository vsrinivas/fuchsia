// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_VM_COW_PAGES_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_VM_COW_PAGES_H_

#include <assert.h>
#include <lib/user_copy/user_ptr.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stdint.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <fbl/array.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <kernel/mutex.h>
#include <vm/page_source.h>
#include <vm/pmm.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <vm/vm_page_list.h>

// Forward declare these so VmCowPages helpers can accept references.
class BatchPQRemove;
class VmObjectPaged;

namespace internal {
struct DiscardableListTag {};
}  // namespace internal

// Implements a copy-on-write hierarchy of pages in a VmPageList.
class VmCowPages final
    : public VmHierarchyBase,
      public fbl::ContainableBaseClasses<
          // Guarded by lock_.
          fbl::TaggedDoublyLinkedListable<VmCowPages*, internal::ChildListTag>,
          // Guarded by DiscardableVmosLock::Get().
          fbl::TaggedDoublyLinkedListable<VmCowPages*, internal::DiscardableListTag>> {
 public:
  static zx_status_t Create(fbl::RefPtr<VmHierarchyState> root_lock, uint32_t pmm_alloc_flags,
                            uint64_t size, fbl::RefPtr<VmCowPages>* cow_pages);

  static zx_status_t CreateExternal(fbl::RefPtr<PageSource> src,
                                    fbl::RefPtr<VmHierarchyState> root_lock, uint64_t size,
                                    fbl::RefPtr<VmCowPages>* cow_pages);

  // Creates a copy-on-write clone with the desired parameters. This can fail due to various
  // internal states not being correct.
  zx_status_t CreateCloneLocked(CloneType type, uint64_t offset, uint64_t size,
                                fbl::RefPtr<VmCowPages>* child_cow) TA_REQ(lock_);

  // Creates a child that looks back to this VmCowPages for all operations. Once a child slice is
  // created this node should not ever be Resized.
  zx_status_t CreateChildSliceLocked(uint64_t offset, uint64_t size,
                                     fbl::RefPtr<VmCowPages>* cow_slice) TA_REQ(lock_);

  // Returns the size in bytes of this cow pages range. This will always be a multiple of the page
  // size.
  uint64_t size_locked() const TA_REQ(lock_) { return size_; }

  // Returns whether this cow pages node is ultimately backed by a user pager to fulfill initial
  // content, and not zero pages.
  bool is_pager_backed_locked() const TA_REQ(lock_) { return GetRootPageSourceLocked() != nullptr; }

  // When attributing pages hidden nodes must be attributed to either their left or right
  // descendants. The attribution IDs of all involved determine where attribution goes. For
  // historical and practical reasons actual user ids are used, although any consistent naming
  // scheme will have the same effect.
  void set_page_attribution_user_id_locked(uint64_t id) TA_REQ(lock_) {
    page_attribution_user_id_ = id;
  }

  // See description on |pinned_page_count_| for meaning.
  uint64_t pinned_page_count_locked() const TA_REQ(lock_) { return pinned_page_count_; }

  // Sets the VmObjectPaged backlink for this copy-on-write node. This object has no tracking of
  // mappings, but understands that they exist. When it manipulates pages in a way that could effect
  // mappings it uses the backlink to notify the VmObjectPaged.
  // Currently it is assumed that all nodes always have backlinks with the 1:1 hierarchy mapping.
  void set_paged_backlink_locked(VmObjectPaged* ref) TA_REQ(lock_) { paged_ref_ = ref; }

  uint64_t HeapAllocationBytesLocked() const TA_REQ(lock_) {
    return page_list_.HeapAllocationBytes();
  }

  uint64_t EvictionEventCountLocked() const TA_REQ(lock_) { return eviction_event_count_; }

  void DetachSourceLocked() TA_REQ(lock_);

  // Resizes the range of this cow pages. |size| must be a multiple of the page size and this must
  // not be called on slices or nodes with slice children.
  zx_status_t ResizeLocked(uint64_t size) TA_REQ(lock_);

  // See VmObject::Lookup
  zx_status_t LookupLocked(uint64_t offset, uint64_t len,
                           fbl::Function<zx_status_t(uint64_t offset, paddr_t pa)> lookup_fn)
      TA_REQ(lock_);

  // See VmObject::TakePages
  zx_status_t TakePagesLocked(uint64_t offset, uint64_t len, VmPageSpliceList* pages) TA_REQ(lock_);

  // See VmObject::SupplyPages
  zx_status_t SupplyPagesLocked(uint64_t offset, uint64_t len, VmPageSpliceList* pages)
      TA_REQ(lock_);

  // See VmObject::FailPageRequests
  zx_status_t FailPageRequestsLocked(uint64_t offset, uint64_t len, zx_status_t error_status)
      TA_REQ(lock_);

  using LookupInfo = VmObject::LookupInfo;
  // See VmObject::GetPage
  // The pages returned from this are assumed to be used in the following ways.
  //  * Our VmObjectPaged backlink, or any of childrens backlinks, are allowed to have readable
  //    mappings, and will be informed to unmap via the backlinks when needed.
  //  * Our VmObjectPaged backlink and our *slice* children are allowed to have writable mappings,
  //    and will be informed to either unmap or remove writability when needed.
  zx_status_t LookupPagesLocked(uint64_t offset, uint pf_flags, uint64_t max_out_pages,
                                list_node* alloc_list, LazyPageRequest* page_request,
                                LookupInfo* out) TA_REQ(lock_);

  // Adds an allocated page to this cow pages at the specified offset, can be optionally zeroed and
  // any mappings invalidated. If an error is returned the caller retains ownership of |page|.
  // Offset must be page aligned.
  zx_status_t AddNewPageLocked(uint64_t offset, vm_page_t* page, bool zero = true,
                               bool do_range_update = true) TA_REQ(lock_);

  // Adds a set of pages consecutively starting from the given offset. Regardless of the return
  // result ownership of the pages is taken. Pages are assumed to be in the ALLOC state and can be
  // optionally zeroed before inserting. start_offset must be page aligned.
  zx_status_t AddNewPagesLocked(uint64_t start_offset, list_node_t* pages, bool zero = true,
                                bool do_range_update = true) TA_REQ(lock_);

  // Attempts to release pages in the pages list causing the range to become copy-on-write again.
  // For consistency if there is a parent or a backing page source, such that the range would not
  // explicitly copy-on-write the zero page then this will fail. Use ZeroPagesLocked for an
  // operation that is guaranteed to succeed, but may not release memory.
  zx_status_t DecommitRangeLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);

  // After successful completion the range of pages will all read as zeros. The mechanism used to
  // achieve this is not guaranteed to decommit, but it will try to.
  // |page_start_base| and |page_end_base| must be page aligned offsets within the range of the
  // object.
  zx_status_t ZeroPagesLocked(uint64_t page_start_base, uint64_t page_end_base) TA_REQ(lock_);

  // Attempts to commit a range of pages. This has three kinds of return status
  //  ZX_OK => The whole range was successfully committed and |len| will be written to
  //           |committed_len|
  //  ZX_ERR_SHOULD_WAIT => A partial (potentially 0) range was committed (output in |committed_len|
  //                        and the passed in |page_request| should be waited on before retrying
  //                        the commit operation. The portion that was successfully committed does
  //                        not need to retried.
  //  * => Any other error, the number of pages committed is undefined.
  // The |offset| and |len| are assumed to be page aligned and within the range of |size_|.
  zx_status_t CommitRangeLocked(uint64_t offset, uint64_t len, uint64_t* committed_len,
                                LazyPageRequest* page_request) TA_REQ(lock_);

  // Increases the pin count of the range of pages given by |offset| and |len|. The full range must
  // already be committed and this either pins all pages in the range, or pins no pages and returns
  // an error. The caller can assume that on success len / PAGE_SIZE pages were pinned.
  // The |offset| and |len| are assumed to be page aligned and within the range of |size_|.
  zx_status_t PinRangeLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);

  // See VmObject::Unpin
  void UnpinLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);

  // Returns true if a page is not currently committed, and if the offset were to be read from, it
  // would be read as zero. Requested offset must be page aligned and within range.
  bool PageWouldReadZeroLocked(uint64_t page_offset) TA_REQ(lock_);

  // Returns whether this node is currently suitable for having a copy-on-write child made of it.
  bool IsCowClonableLocked() const TA_REQ(lock_);

  // see VmObjectPaged::AttributedPagesInRange
  size_t AttributedPagesInRangeLocked(uint64_t offset, uint64_t len) const TA_REQ(lock_);

  // Scans this cow pages range for zero pages and frees them if |reclaim| is set to true. Returns
  // the number of pages freed or scanned.
  uint32_t ScanForZeroPagesLocked(bool reclaim) TA_REQ(lock_);

  enum class EvictionHintAction : uint8_t {
    Follow,
    Ignore,
  };

  // Asks the VMO to attempt to evict the specified page. This returns true if the page was
  // actually from this VMO and was successfully evicted, at which point the caller now has
  // ownership of the page. Otherwise eviction is allowed to fail for any reason, specifically
  // if the page is considered in use, or the VMO has no way to recreate the page then eviction
  // will fail. Although eviction may fail for any reason, if it does the caller is able to assume
  // that either the page was not from this vmo, or that the page is not in any evictable page queue
  // (such as the pager_backed_ queue).
  // |hint_action| indicates whether the |always_need| eviction hint should be respected or ignored.
  // If this page is not evicted as a result of the hint, the caller can assume that the page has
  // been moved out from the evictable page queue(s) into the active queue(s).
  bool RemovePageForEviction(vm_page_t* page, uint64_t offset, EvictionHintAction hint_action);

  // Attempts to dedup the given page at the specified offset with the zero page. The only
  // correctness requirement for this is that `page` must be *some* valid vm_page_t, meaning that
  // all race conditions are handled internally. This function returns false if
  //  * page is either not from this VMO, or not found at the specified offset
  //  * page is pinned
  //  * vmo is uncached
  //  * page is not all zeroes
  // Otherwise 'true' is returned and the page will have been returned to the pmm with a zero page
  // marker put in its place.
  bool DedupZeroPage(vm_page_t* page, uint64_t offset);

  void DumpLocked(uint depth, bool verbose) const TA_REQ(lock_);
  bool DebugValidatePageSplitsLocked() const TA_REQ(lock_);
  // Calls DebugValidatePageSplitsLocked on this and every parent in the chain, returning true if
  // all return true.
  bool DebugValidatePageSplitsHierarchyLocked() const TA_REQ(lock_);

  // Different operations that RangeChangeUpdate* can perform against any VmMappings that are found.
  enum class RangeChangeOp {
    Unmap,
    RemoveWrite,
  };
  // Apply the specified operation to all mappings in the given range. This is applied to all
  // descendants within the range.
  void RangeChangeUpdateLocked(uint64_t offset, uint64_t len, RangeChangeOp op) TA_REQ(lock_);

  // Promote pages in the specified range for reclamation under memory pressure. |offset| will be
  // rounded down to the page boundary, and |len| will be rounded up to the page boundary.
  // Currently used only for pager-backed VMOs to move their pages to the end of the
  // pager-backed queue, so that they can be evicted first.
  void PromoteRangeForReclamationLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);

  // Protect pages in the specified range from reclamation under memory pressure. |offset| will be
  // rounded down to the page boundary, and |len| will be rounded up to the page boundary. Used to
  // set the |always_need| hint for pages in pager-backed VMOs. Any absent pages in the range will
  // be committed first, and the call will block on the fulfillment of the page request(s), dropping
  // |guard| while waiting (multiple times if multiple pages need to be supplied).
  void ProtectRangeFromReclamationLocked(uint64_t offset, uint64_t len, Guard<Mutex>* guard)
      TA_REQ(lock_);

  // Helper function to hint always_need on a |page| (if applicable).
  void HintAlwaysNeedLocked(vm_page_t* page) const TA_REQ(lock_);

  zx_status_t LockRangeLocked(uint64_t offset, uint64_t len, zx_vmo_lock_state_t* lock_state_out);
  zx_status_t TryLockRangeLocked(uint64_t offset, uint64_t len);
  zx_status_t UnlockRangeLocked(uint64_t offset, uint64_t len);

  // Exposed for testing.
  uint64_t DebugGetLockCount() const {
    Guard<Mutex> guard{&lock_};
    return lock_count_;
  }
  bool DebugIsReclaimable() const;
  bool DebugIsUnreclaimable() const;
  bool DebugIsDiscarded() const;

  // Discard all the pages from a discardable vmo in the |kReclaimable| state. For this call to
  // succeed, the vmo should have been in the reclaimable state for at least
  // |min_duration_since_reclaimable|. If successful, the |discardable_state_| is set to
  // |kDiscarded|, and the vmo is moved from the reclaim candidates list. The pages are removed /
  // discarded from the vmo and appended to the |freed_list| passed in; the caller takes ownership
  // of the removed pages and is responsible for freeing them. Returns the number of pages
  // discarded.
  uint64_t DiscardPages(zx_duration_t min_duration_since_reclaimable, list_node_t* freed_list)
      TA_EXCL(DiscardableVmosLock::Get()) TA_EXCL(lock_);

  struct DiscardablePageCounts {
    uint64_t locked;
    uint64_t unlocked;
  };

  // Returns the total number of pages locked and unlocked across all discardable vmos.
  // Note that this might not be exact and we might miss some vmos, because the
  // |DiscardableVmosLock| is dropped after processing each vmo on the global discardable lists.
  // That is fine since these numbers are only used for accounting.
  static DiscardablePageCounts DebugDiscardablePageCounts() TA_EXCL(DiscardableVmosLock::Get());

  // Walks through the LRU reclaimable list of discardable vmos and discards pages from each, until
  // |target_pages| have been discarded, or the list of candidates is exhausted. Only vmos that have
  // become reclaimable more than |min_duration_since_reclaimable| in the past will be discarded;
  // this prevents discarding reclaimable vmos that were recently accessed. The discarded pages are
  // appended to the |freed_list| passed in; the caller takes ownership of the discarded pages and
  // is responsible for freeing them. Returns the total number of pages discarded.
  static uint64_t ReclaimPagesFromDiscardableVmos(uint64_t target_pages,
                                                  zx_duration_t min_duration_since_reclaimable,
                                                  list_node_t* freed_list)
      TA_EXCL(DiscardableVmosLock::Get());

  // Walks up the parent tree and returns the root, or |this| if there is no parent.
  const VmCowPages* GetRootLocked() const TA_REQ(lock_);

 private:
  // private constructor (use Create())
  VmCowPages(fbl::RefPtr<VmHierarchyState> root_lock, uint32_t options, uint32_t pmm_alloc_flags,
             uint64_t size, fbl::RefPtr<PageSource> page_source);

  // private destructor, only called from refptr
  ~VmCowPages() override;
  friend fbl::RefPtr<VmCowPages>;

  DISALLOW_COPY_ASSIGN_AND_MOVE(VmCowPages);

  bool is_hidden_locked() const TA_REQ(lock_) { return (options_ & kHidden); }
  bool is_slice_locked() const TA_REQ(lock_) { return options_ & kSlice; }

  // Add a page to the object. This operation unmaps the corresponding
  // offset from any existing mappings.
  // If |do_range_update| is false, this function will skip updating mappings.
  // On success the page to add is moved out of `*p`, otherwise it is left there.
  zx_status_t AddPageLocked(VmPageOrMarker* p, uint64_t offset, bool do_range_update = true)
      TA_REQ(lock_);

  // Unmaps and removes all the committed pages in the specified range.
  // Called from DecommitRangeLocked() to perform the actual decommit action after some of the
  // initial sanity checks have succeeded. Also called from DetachSourceLocked() when a VMO is
  // detached from the page source, and from DiscardPages() to reclaim pages from a discardable VMO.
  // Upon success the removed pages are placed in |freed_list|. The caller has ownership of these
  // pages and is responsible for freeing them.
  //
  // Unlike DecommitRangeLocked(), this function only operates on |this| node, which must have no
  // parent.
  // |offset| must be page aligned. |len| must be less than or equal to |size_ - offset|. If |len|
  // is less than |size_ - offset| it must be page aligned.
  // Optionally returns the number of pages removed if |pages_freed_out| is not null.
  zx_status_t UnmapAndRemovePagesLocked(uint64_t offset, uint64_t len, list_node_t* freed_list,
                                        uint64_t* pages_freed_out = nullptr) TA_REQ(lock_);

  // internal check if any pages in a range are pinned
  bool AnyPagesPinnedLocked(uint64_t offset, size_t len) TA_REQ(lock_);

  // Helper function for ::AllocatedPagesInRangeLocked. Counts the number of pages in ancestor's
  // vmos that should be attributed to this vmo for the specified range. It is an error to pass in a
  // range that does not need attributing (i.e. offset must be < parent_limit_), although |len| is
  // permitted to be sized such that the range exceeds parent_limit_.
  // The return value is the length of the processed region, which will be <= |size| and is
  // guaranteed to be > 0. The |count| is the number of pages in this region that should be
  // attributed to this vmo, versus some other vmo.
  uint64_t CountAttributedAncestorPagesLocked(uint64_t offset, uint64_t size, uint64_t* count) const
      TA_REQ(lock_);

  // Searches for the the initial content for |this| at |offset|. The result could be used to
  // initialize a commit, or compare an existing commit with the original. The initial content
  // is a reference to a VmPageOrMarker as there could be an explicit vm_page of content, an
  // explicit zero page of content via a marker, or no initial content. Determining the meaning of
  // no initial content (i.e. whether it is zero or something else) is left up to the caller.
  //
  // If an ancestor has a committed page which corresponds to |offset|, returns that page
  // as well as the VmCowPages and offset which own the page. If no ancestor has a committed
  // page for the offset, returns null as well as the VmCowPages/offset which need to be queried
  // to populate the page.
  //
  // If the passed |owner_length| is not null, then the visible range of the owner is calculated and
  // stored back into |owner_length| on the walk up. The |owner_length| represents the size of the
  // range in the owner for which no other VMO in the chain had forked a page.
  VmPageOrMarker* FindInitialPageContentLocked(uint64_t offset, VmCowPages** owner_out,
                                               uint64_t* owner_offset_out, uint64_t* owner_length)
      TA_REQ(lock_);

  // LookupPagesLocked helper function that 'forks' the page at |offset| of the current vmo. If
  // this function successfully inserts a page into |offset| of the current vmo, it returns
  // a pointer to the corresponding vm_page_t struct. The only failure condition is memory
  // allocation failure, in which case this function returns null.
  //
  // The source page that is being forked has already been calculated - it is |page|, which
  // is currently in |page_owner| at offset |owner_offset|.
  //
  // This function is responsible for ensuring that COW clones never result in worse memory
  // consumption than simply creating a new vmo and memcpying the content. It does this by
  // migrating a page from a hidden vmo into one child if that page is not 'accessible' to the
  // other child (instead of allocating a new page into the child and making the hidden vmo's
  // page inaccessible).
  //
  // Whether a particular page in a hidden vmo is 'accessible' to a particular child is
  // determined by a combination of two factors. First, if the page lies outside of the range
  // in the hidden vmo the child can see (specified by parent_offset_ and parent_limit_), then
  // the page is not accessible. Second, if the page has already been copied into the child,
  // then the page in the hidden vmo is not accessible to that child. This is tracked by the
  // cow_X_split bits in the vm_page_t structure.
  //
  // To handle memory allocation failure, this function performs the fork operation from the
  // root vmo towards the leaf vmo. This allows the COW invariants to always be preserved.
  //
  // |page| must not be the zero-page, as there is no need to do the complex page
  // fork logic to reduce memory consumption in that case.
  vm_page_t* CloneCowPageLocked(uint64_t offset, list_node_t* alloc_list, VmCowPages* page_owner,
                                vm_page_t* page, uint64_t owner_offset) TA_REQ(lock_);

  // This is an optimized wrapper around CloneCowPageLocked for when an initial content page needs
  // to be forked to preserve the COW invariant, but you know you are immediately going to overwrite
  // the forked page with zeros.
  //
  // The optimization it can make is that it can fork the page up to the parent and then, instead
  // of forking here and then having to immediately free the page, it can insert a marker here and
  // set the split bits in the parent page as if it had been forked.
  zx_status_t CloneCowPageAsZeroLocked(uint64_t offset, list_node_t* freed_list,
                                       VmCowPages* page_owner, vm_page_t* page,
                                       uint64_t owner_offset) TA_REQ(lock_);

  // Returns true if |page| (located at |offset| in this vmo) is only accessible by one
  // child, where 'accessible' is defined by ::CloneCowPageLocked.
  bool IsUniAccessibleLocked(vm_page_t* page, uint64_t offset) const TA_REQ(lock_);

  // Releases this vmo's reference to any ancestor vmo's COW pages, for the range [start, end)
  // in this vmo. This is done by either setting the pages' split bits (if something else
  // can access the pages) or by freeing the pages using the |page_remover|
  //
  // This function recursively invokes itself for regions of the parent vmo which are
  // not accessible by the sibling vmo.
  void ReleaseCowParentPagesLocked(uint64_t start, uint64_t end, BatchPQRemove* page_remover)
      TA_REQ(lock_);

  // Helper function for ReleaseCowParentPagesLocked that processes pages which are visible
  // to at least this VMO, and possibly its sibling, as well as updates parent_(offset_)limit_.
  void ReleaseCowParentPagesLockedHelper(uint64_t start, uint64_t end, bool sibling_visible,
                                         BatchPQRemove* page_remover) TA_REQ(lock_);

  // Updates the parent limits of all children so that they will never be able to
  // see above |new_size| in this vmo, even if the vmo is enlarged in the future.
  void UpdateChildParentLimitsLocked(uint64_t new_size) TA_REQ(lock_);

  // When cleaning up a hidden vmo, merges the hidden vmo's content (e.g. page list, view
  // of the parent) into the remaining child.
  void MergeContentWithChildLocked(VmCowPages* removed, bool removed_left) TA_REQ(lock_);

  // Only valid to be called when is_slice_locked() is true and returns the first parent of this
  // hierarchy that is not a slice. The offset of this slice within that VmObjectPaged is set as
  // the output.
  VmCowPages* PagedParentOfSliceLocked(uint64_t* offset) TA_REQ(lock_);

  // Unpins a page and potentially moves it into a different page queue should its pin
  // count reach zero.
  void UnpinPage(vm_page_t* page, uint64_t offset);

  // Updates the page queue of an existing page, moving it to whichever non wired queue
  // is appropriate.
  void MoveToNotWired(vm_page_t* page, uint64_t offset);

  // Places a newly added page into the appropriate non wired page queue.
  void SetNotWired(vm_page_t* page, uint64_t offset);

  // Updates any meta data for accessing a page. Currently this moves pager backed pages around in
  // the page queue to track which ones were recently accessed for the purposes of eviction. In
  // terms of functional correctness this never has to be called.
  void UpdateOnAccessLocked(vm_page_t* page, uint pf_flags) TA_REQ(lock_);

  // Initializes and adds as a child the given VmCowPages as a full clone of this one such that the
  // VmObjectPaged backlink can be moved from this to the child, keeping all page offsets, sizes and
  // other requirements (see VmObjectPaged::SetCowPagesReferenceLocked) are valid. This does also
  // move our paged_ref_ into child_ and update the VmObjectPaged backlinks.
  void CloneParentIntoChildLocked(fbl::RefPtr<VmCowPages>& child) TA_REQ(lock_);

  // Removes the specified child from this objects |children_list_| and performs any hierarchy
  // updates that need to happen as a result. This does not modify the |parent_| member of the
  // removed child and if this is not being called due to |removed| being destructed it is the
  // callers responsibility to correct parent_.
  void RemoveChildLocked(VmCowPages* removed) TA_REQ(lock_);

  // Inserts a newly created VmCowPages into this hierarchy as a child of this VmCowPages.
  // Initializes child members based on the passed in values that only have meaning when an object
  // is a child. This updates the parent_ field in child to hold a refptr to |this|.
  void AddChildLocked(VmCowPages* child, uint64_t offset, uint64_t root_parent_offset,
                      uint64_t parent_limit) TA_REQ(lock_);

  // Outside of initialization/destruction, hidden vmos always have two children. For
  // clarity, whichever child is first in the list is the 'left' child, and whichever
  // child is second is the 'right' child. Children of a paged vmo will always be paged
  // vmos themselves.
  VmCowPages& left_child_locked() TA_REQ(lock_) TA_ASSERT(left_child_locked().lock()) {
    DEBUG_ASSERT(is_hidden_locked());
    DEBUG_ASSERT(children_list_len_ == 2);

    auto& ret = children_list_.front();
    AssertHeld(ret.lock_);
    return ret;
  }
  VmCowPages& right_child_locked() TA_REQ(lock_) TA_ASSERT(right_child_locked().lock()) {
    DEBUG_ASSERT(is_hidden_locked());
    DEBUG_ASSERT(children_list_len_ == 2);
    auto& ret = children_list_.back();
    AssertHeld(ret.lock_);
    return ret;
  }
  const VmCowPages& left_child_locked() const TA_REQ(lock_) TA_ASSERT(left_child_locked().lock()) {
    DEBUG_ASSERT(is_hidden_locked());
    DEBUG_ASSERT(children_list_len_ == 2);
    const auto& ret = children_list_.front();
    AssertHeld(ret.lock_);
    return ret;
  }
  const VmCowPages& right_child_locked() const TA_REQ(lock_)
      TA_ASSERT(right_child_locked().lock()) {
    DEBUG_ASSERT(is_hidden_locked());
    DEBUG_ASSERT(children_list_len_ == 2);
    const auto& ret = children_list_.back();
    AssertHeld(ret.lock_);
    return ret;
  }

  void ReplaceChildLocked(VmCowPages* old, VmCowPages* new_child) TA_REQ(lock_);

  void DropChildLocked(VmCowPages* c) TA_REQ(lock_);

  // Types for an additional linked list over the VmCowPages for use when doing a
  // RangeChangeUpdate.
  //
  // To avoid unbounded stack growth we need to reserve the memory to exist on a
  // RangeChange list in our object so that we can have a flat iteration over a
  // work list. RangeChangeLists should only be used by the RangeChangeUpdate
  // code.
  using RangeChangeNodeState = fbl::SinglyLinkedListNodeState<VmCowPages*>;
  struct RangeChangeTraits {
    static RangeChangeNodeState& node_state(VmCowPages& cow) { return cow.range_change_state_; }
  };
  using RangeChangeList =
      fbl::SinglyLinkedListCustomTraits<VmCowPages*, VmCowPages::RangeChangeTraits>;
  friend struct RangeChangeTraits;

  // Given an initial list of VmCowPages performs RangeChangeUpdate on it until the list is empty.
  static void RangeChangeUpdateListLocked(RangeChangeList* list, RangeChangeOp op);

  void RangeChangeUpdateFromParentLocked(uint64_t offset, uint64_t len, RangeChangeList* list)
      TA_REQ(lock_);

  // Helper to check whether the requested range for LockRangeLocked() / TryLockRangeLocked() /
  // UnlockRangeLocked() is valid.
  bool IsLockRangeValidLocked(uint64_t offset, uint64_t len) const TA_REQ(lock_);

  // Lock that protects the global discardable lists.
  // This lock can be acquired with the vmo's |lock_| held. To prevent deadlocks, if both locks are
  // required the order of locking should always be 1) vmo's lock, and then 2) DiscardableVmosLock.
  DECLARE_SINGLETON_MUTEX(DiscardableVmosLock);

  enum class DiscardableState : uint8_t {
    kUnset = 0,
    kReclaimable,
    kUnreclaimable,
    kDiscarded,
  };

  using DiscardableList = fbl::TaggedDoublyLinkedList<VmCowPages*, internal::DiscardableListTag>;

  // Two global lists of discardable vmos:
  // - |discardable_reclaim_candidates_| tracks discardable vmos that are eligible for reclamation
  // and haven't been reclaimed yet.
  // - |discardable_non_reclaim_candidates_| tracks all other discardable VMOs.
  // The lists are protected by the |DiscardableVmosLock|, and updated based on a discardable vmo's
  // state changes (lock, unlock, or discard).
  static DiscardableList discardable_reclaim_candidates_ TA_GUARDED(DiscardableVmosLock::Get());
  static DiscardableList discardable_non_reclaim_candidates_ TA_GUARDED(DiscardableVmosLock::Get());

  // Helper function to move an object from the |discardable_non_reclaim_candidates_| list to the
  // |discardable_reclaim_candidates_| list.
  void MoveToReclaimCandidatesListLocked() TA_REQ(lock_) TA_REQ(DiscardableVmosLock::Get());

  // Helper function to move an object from the |discardable_reclaim_candidates_| list to the
  // |discardable_non_reclaim_candidates_| list. If |new_candidate| is true, that indicates that the
  // object was not yet being tracked on any list, and should only be inserted into the
  // |discardable_non_reclaim_candidates_| list without a corresponding list removal.
  void MoveToNonReclaimCandidatesListLocked(bool new_candidate = false) TA_REQ(lock_)
      TA_REQ(DiscardableVmosLock::Get());

  // Updates the |discardable_state_| of a discardable vmo, and moves it from one discardable list
  // to another.
  void UpdateDiscardableStateLocked(DiscardableState state) TA_REQ(lock_)
      TA_EXCL(DiscardableVmosLock::Get());

  // Remove a discardable object from whichever global discardable list it is in. Called from the
  // VmCowPages destructor.
  void RemoveFromDiscardableListLocked() TA_REQ(lock_) TA_EXCL(DiscardableVmosLock::Get());

  // Returns whether the vmo is in either one of the |discardable_reclaim_candidates_| or
  // |discardable_reclaim_candidates_| lists, depending on whether it is a |reclaim_candidate|
  // or not.
  bool DebugIsInDiscardableListLocked(bool reclaim_candidate) const TA_REQ(lock_)
      TA_EXCL(DiscardableVmosLock::Get());

  DiscardablePageCounts GetDiscardablePageCounts() const TA_EXCL(lock_);

  // Returns the root parent's page source.
  fbl::RefPtr<PageSource> GetRootPageSourceLocked() const TA_REQ(lock_);

  // magic value
  fbl::Canary<fbl::magic("VMCP")> canary_;

  // |options_| is a bitmask of:
  static constexpr uint32_t kHidden = (1u << 2);
  static constexpr uint32_t kSlice = (1u << 3);
  uint32_t options_ TA_GUARDED(lock_);

  uint64_t size_ TA_GUARDED(lock_);
  // Offset in the *parent* where this object starts.
  uint64_t parent_offset_ TA_GUARDED(lock_) = 0;
  // Offset in *this object* above which accesses will no longer access the parent.
  uint64_t parent_limit_ TA_GUARDED(lock_) = 0;
  // Offset in *this object* below which this vmo stops referring to its parent. This field
  // is only useful for hidden vmos, where it is used by ::ReleaseCowPagesParentLocked
  // together with parent_limit_ to reduce how often page split bits need to be set. It is
  // effectively a summary of the parent_offset_ values of all descendants - unlike
  // parent_limit_, this value does not directly impact page lookup. See partial_cow_release_ flag
  // for more details on usage of this limit.
  uint64_t parent_start_limit_ TA_GUARDED(lock_) = 0;
  // Offset in our root parent where this object would start if projected onto it. This value is
  // used as an efficient summation of accumulated offsets to ensure that an offset projected all
  // the way to the root would not overflow a 64-bit integer. Although actual page resolution
  // would never reach the root in such a case, a childs full range projected onto its parent is
  // used to simplify some operations and so this invariant of not overflowing accumulated offsets
  // needs to be maintained.
  uint64_t root_parent_offset_ TA_GUARDED(lock_) = 0;
  const uint32_t pmm_alloc_flags_;

  // Flag which is true if there was a call to ::ReleaseCowParentPagesLocked which was
  // not able to update the parent limits. When this is not set, it is sometimes
  // possible for ::MergeContentWithChildLocked to do significantly less work. This flag acts as a
  // proxy then for how precise the parent_limit_ and parent_start_limit_ are. It is always an
  // absolute guarantee that descendants cannot see outside of the limits, but when this flag is
  // true there is a possibility that there is a sub range inside the limits that they also cannot
  // see.
  // Imagine a two siblings that see the parent range [0x1000-0x2000) and [0x3000-0x4000)
  // respectively. The parent can have the start_limit of 0x1000 and limit of 0x4000, but without
  // additional allocations it cannot track the free region 0x2000-0x3000, and so
  // partial_cow_release_ must be set to indicate in the future we need to do more expensive
  // processing to check for such free regions.
  bool partial_cow_release_ TA_GUARDED(lock_) = false;

  // parent pointer (may be null)
  fbl::RefPtr<VmCowPages> parent_ TA_GUARDED(lock_);

  // list of every child
  fbl::TaggedDoublyLinkedList<VmCowPages*, internal::ChildListTag> children_list_ TA_GUARDED(lock_);

  // length of children_list_
  uint32_t children_list_len_ TA_GUARDED(lock_) = 0;

  // Flag used for walking back up clone tree without recursion. See ::CloneCowPageLocked.
  enum class StackDir : bool {
    Left,
    Right,
  };
  struct {
    uint64_t scratch : 63;
    StackDir dir_flag : 1;
  } stack_ TA_GUARDED(lock_);

  // This value is used when determining against which user-visible vmo a hidden vmo's
  // pages should be attributed. It serves as a tie-breaker for pages that are accessible by
  // multiple user-visible vmos. See ::HasAttributedAncestorPageLocked for more details.
  //
  // For non-hidden vmobjects, this always equals user_id_. For hidden vmobjects, this
  // is the page_attribution_user_id_ of one of their children (i.e. the user_id_ of one
  // of their non-hidden descendants).
  uint64_t page_attribution_user_id_ TA_GUARDED(lock_) = 0;

  // Counts the total number of pages pinned by ::CommitRange. If one page is pinned n times, it
  // contributes n to this count.
  uint64_t pinned_page_count_ TA_GUARDED(lock_) = 0;

  // The page source, if any.
  const fbl::RefPtr<PageSource> page_source_;

  // Count eviction events so that we can report them to the user.
  uint64_t eviction_event_count_ TA_GUARDED(lock_) = 0;

  // Count of outstanding lock operations. A non-zero count prevents the kernel from discarding /
  // evicting pages from the VMO to relieve memory pressure (currently only applicable if
  // |kDiscardable| is set). Note that this does not prevent removal of pages by other means, like
  // decommitting or resizing, since those are explicit actions driven by the user, not by the
  // kernel directly.
  uint64_t lock_count_ TA_GUARDED(lock_) = 0;

  // Timestamp of the last unlock operation that changed a discardable vmo's state to
  // |kReclaimable|. Used to determine whether the vmo was accessed too recently to be discarded.
  zx_time_t last_unlock_timestamp_ TA_GUARDED(lock_) = ZX_TIME_INFINITE;

  // The current state of a discardable vmo, depending on the lock count and whether it has been
  // discarded.
  // State transitions work as follows:
  // 1. kUnreclaimable -> kReclaimable: When the lock count changes from 1 to 0.
  // 2. kReclaimable -> kUnreclaimable: When the lock count changes from 0 to 1. The vmo remains
  // kUnreclaimable for any non-zero lock count.
  // 3. kReclaimable -> kDiscarded: When a vmo with lock count 0 is discarded.
  // 4. kDiscarded -> kUnreclaimable: When a discarded vmo is locked again.
  //
  // We start off with state kUnset, so a discardable vmo must be locked at least once to opt into
  // the above state transitions. For non-discardable vmos, the state will always remain kUnset.
  DiscardableState discardable_state_ TA_GUARDED(lock_) = DiscardableState::kUnset;

  // a tree of pages
  VmPageList page_list_ TA_GUARDED(lock_);

  RangeChangeNodeState range_change_state_;
  uint64_t range_change_offset_ TA_GUARDED(lock_);
  uint64_t range_change_len_ TA_GUARDED(lock_);

  // optional reference back to a VmObjectPaged so that we can perform mapping updates. This is a
  // raw pointer to avoid circular references, the VmObjectPaged destructor needs to update it.
  VmObjectPaged* paged_ref_ TA_GUARDED(lock_) = nullptr;

  using Cursor =
      VmoCursor<VmCowPages, DiscardableVmosLock, DiscardableList, DiscardableList::iterator>;

  // The list of all outstanding cursors iterating over the discardable lists:
  // |discardable_reclaim_candidates_| and |discardable_non_reclaim_candidates_|. The cursors should
  // be advanced (by calling AdvanceIf()) before removing any element from the discardable lists.
  static fbl::DoublyLinkedList<Cursor*> discardable_vmos_cursors_
      TA_GUARDED(DiscardableVmosLock::Get());
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_VM_COW_PAGES_H_
