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
#include <fbl/enum_bits.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <kernel/mutex.h>
#include <vm/page_source.h>
#include <vm/physical_page_borrowing_config.h>
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

enum class VmCowPagesOptions : uint32_t {
  // Externally-usable flags:
  kNone = 0u,

  // With this clear, zeroing a page tries to decommit the page.  With this set, zeroing never
  // decommits the page.  Currently this is only set for contiguous VMOs.
  //
  // TODO(dustingreen): Once we're happy with the reliability of page borrowing, we should be able
  // to relax this restriction.  We may still need to flush zeroes to RAM during reclaim to mitigate
  // a hypothetical client incorrectly assuming that cache-clean status will remain intact while
  // pages aren't pinned, but that mitigation should be sufficient (even assuming such a client) to
  // allow implicit decommit when zeroing or when zero scanning, as long as no clients are doing DMA
  // to/from contiguous while not pinned.
  kCannotDecommitZeroPages = (1u << 0),

  // Internal-only flags:
  kHidden = (1u << 1),
  kSlice = (1u << 2),
  kUnpinOnDelete = (1u << 3),

  kInternalOnlyMask = kHidden | kSlice,
};
FBL_ENABLE_ENUM_BITS(VmCowPagesOptions)

// Implements a copy-on-write hierarchy of pages in a VmPageList.
class VmCowPages final
    : public VmHierarchyBase,
      public fbl::ContainableBaseClasses<
          // Guarded by lock_.
          fbl::TaggedDoublyLinkedListable<VmCowPages*, internal::ChildListTag>,
          // Guarded by DiscardableVmosLock::Get().
          fbl::TaggedDoublyLinkedListable<VmCowPages*, internal::DiscardableListTag>>,
      public fbl::Recyclable<VmCowPages> {
 public:
  static zx_status_t Create(fbl::RefPtr<VmHierarchyState> root_lock, VmCowPagesOptions options,
                            uint32_t pmm_alloc_flags, uint64_t size,
                            fbl::RefPtr<VmCowPages>* cow_pages);

  static zx_status_t CreateExternal(fbl::RefPtr<PageSource> src, VmCowPagesOptions options,
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
  // content, and not zero pages.  Contiguous VMOs have page_source_ set, but are not pager backed
  // in this sense.
  //
  // This should only be used to report to user mode whether a VMO is user-pager backed, not for any
  // other purpose.
  bool is_root_source_user_pager_backed_locked() const TA_REQ(lock_) {
    auto root = GetRootLocked();
    // The root will never be null. It will either point to a valid parent, or |this| if there's no
    // parent.
    DEBUG_ASSERT(root);
    return root->page_source_ && root->page_source_->properties().is_user_pager;
  }

  bool debug_is_user_pager_backed_locked() const TA_REQ(lock_) {
    return page_source_ && page_source_->properties().is_user_pager;
  }

  bool debug_is_contiguous() const TA_REQ(lock_) {
    return page_source_ && page_source_->properties().is_providing_specific_physical_pages;
  }

  bool is_private_pager_copy_supported() const TA_REQ(lock_) {
    auto root = GetRootLocked();
    // The root will never be null. It will either point to a valid parent, or |this| if there's no
    // parent.
    DEBUG_ASSERT(root);
    bool result = root->page_source_ && root->page_source_->properties().is_preserving_page_content;
    DEBUG_ASSERT(result == is_root_source_user_pager_backed_locked());
    return result;
  }

  bool is_cow_clonable_locked() const TA_REQ(lock_) {
    // Copy-on-write clones of pager vmos or their descendants aren't supported as we can't
    // efficiently make an immutable snapshot.
    if (can_root_source_evict_locked()) {
      return false;
    }

    // We also don't support COW clones for contiguous VMOs.
    if (is_source_supplying_specific_physical_pages_locked()) {
      return false;
    }

    // Copy-on-write clones of slices aren't supported at the moment due to the resulting VMO chains
    // having non hidden VMOs between hidden VMOs. This case cannot be handled be CloneCowPageLocked
    // at the moment and so we forbid the construction of such cases for the moment.
    // Bug: 36841
    if (is_slice_locked()) {
      return false;
    }

    return true;
  }

  bool can_evict_locked() const TA_REQ(lock_) {
    bool result = page_source_ && page_source_->properties().is_preserving_page_content;
    DEBUG_ASSERT(result == debug_is_user_pager_backed_locked());
    return result;
  }

  bool can_root_source_evict_locked() const TA_REQ(lock_) {
    auto root = GetRootLocked();
    // The root will never be null. It will either point to a valid parent, or |this| if there's no
    // parent.
    DEBUG_ASSERT(root);
    AssertHeld(root->lock_);
    bool result = root->can_evict_locked();
    DEBUG_ASSERT(result == is_root_source_user_pager_backed_locked());
    return result;
  }

  bool has_pager_backlinks_locked() const TA_REQ(lock_) {
    bool result = can_evict_locked();
    DEBUG_ASSERT(result == debug_is_user_pager_backed_locked());
    return result;
  }

  // Returns whether this cow pages node is dirty tracked.
  bool is_dirty_tracked_locked() const TA_REQ(lock_) {
    // Pager-backed VMOs require dirty tracking either if:
    // 1. They are directly backed by the pager, i.e. the root VMO.
    // OR
    // 2. They are slice children of root pager-backed VMOs, since slices directly reference the
    // parent's pages.
    auto* which_cow = is_slice_locked() ? parent_.get() : this;
    bool result =
        which_cow->page_source_ && which_cow->page_source_->properties().is_preserving_page_content;
    AssertHeld(which_cow->lock_);
    DEBUG_ASSERT(result == which_cow->debug_is_user_pager_backed_locked());
    return result;
  }

  bool is_source_preserving_page_content_locked() const TA_REQ(lock_) {
    bool result = page_source_ && page_source_->properties().is_preserving_page_content;
    DEBUG_ASSERT(result == debug_is_user_pager_backed_locked());
    return result;
  }

  bool is_source_supplying_specific_physical_pages_locked() const TA_REQ(lock_) {
    bool result = page_source_ && page_source_->properties().is_providing_specific_physical_pages;
    DEBUG_ASSERT(result == debug_is_contiguous());
    return result;
  }

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
  zx_status_t LookupLocked(uint64_t offset, uint64_t len, VmObject::LookupFunction lookup_fn)
      TA_REQ(lock_);

  // See VmObject::TakePages
  zx_status_t TakePagesLocked(uint64_t offset, uint64_t len, VmPageSpliceList* pages) TA_REQ(lock_);

  // See VmObject::SupplyPages
  //
  // The new_zeroed_pages parameter should be true if the pages are new pages that need to be
  // initialized, or false if the pages are from a different VmCowPages and are being moved to this
  // VmCowPages.
  zx_status_t SupplyPagesLocked(uint64_t offset, uint64_t len, VmPageSpliceList* pages,
                                bool new_zeroed_pages) TA_REQ(lock_);

  // The new_zeroed_pages parameter should be true if the pages are new pages that need to be
  // initialized, or false if the pages are from a different VmCowPages and are being moved to this
  // VmCowPages.
  zx_status_t SupplyPages(uint64_t offset, uint64_t len, VmPageSpliceList* pages,
                          bool new_zeroed_pages) TA_EXCL(lock_);

  // See VmObject::FailPageRequests
  zx_status_t FailPageRequestsLocked(uint64_t offset, uint64_t len, zx_status_t error_status)
      TA_REQ(lock_);

  // Used to track dirty_state in the vm_page_t.
  //
  // The transitions between the three states can roughly be summarized as follows:
  // 1. A page starts off as Clean when supplied.
  // 2. A write transitions the page from Clean to Dirty.
  // 3. A writeback_begin moves the Dirty page to AwaitingClean.
  // 4. A writeback_end moves the AwaitingClean page to Clean.
  // 5. A write that comes in while the writeback is in progress (i.e. the page is AwaitingClean)
  // moves the AwaitingClean page back to Dirty.
  enum class DirtyState : uint8_t {
    // The page does not track dirty state. Used for non pager backed pages.
    Untracked = 0,
    // The page is clean, i.e. its contents have not been altered from when the page was supplied.
    Clean,
    // The page's contents have been modified from the time of supply, and should be written back to
    // the page source at some point.
    Dirty,
    // The page still has modified contents, but the page source is in the process of writing back
    // the changes. This is used to ensure that a consistent version is written back, and that any
    // new modifications that happen during the writeback are not lost. The page source will mark
    // pages AwaitingClean before starting any writeback.
    AwaitingClean,
    NumStates,
  };
  // Make sure that the state can be encoded in the vm_page_t's dirty_state field.
  static_assert(static_cast<uint8_t>(DirtyState::NumStates) <= VM_PAGE_OBJECT_MAX_DIRTY_STATES);

  static bool is_page_dirty_tracked(const vm_page_t* page) {
    return DirtyState(page->object.dirty_state) != DirtyState::Untracked;
  }
  static bool is_page_dirty(const vm_page_t* page) {
    return DirtyState(page->object.dirty_state) == DirtyState::Dirty;
  }
  static bool is_page_clean(const vm_page_t* page) {
    return DirtyState(page->object.dirty_state) == DirtyState::Clean;
  }
  static bool is_page_awaiting_clean(const vm_page_t* page) {
    return DirtyState(page->object.dirty_state) == DirtyState::AwaitingClean;
  }

  // See VmObject::DirtyPages
  zx_status_t DirtyPagesLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);

  using DirtyRangeEnumerateFunction = VmObject::DirtyRangeEnumerateFunction;
  // See VmObject::EnumerateDirtyRanges
  zx_status_t EnumerateDirtyRangesLocked(uint64_t offset, uint64_t len,
                                         DirtyRangeEnumerateFunction&& dirty_range_fn) const
      TA_REQ(lock_);

  // See VmObject::WritebackBegin
  zx_status_t WritebackBeginLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);

  // See VmObject::WritebackEnd
  zx_status_t WritebackEndLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);

  using LookupInfo = VmObject::LookupInfo;
  using DirtyTrackingAction = VmObject::DirtyTrackingAction;
  // See VmObject::GetPage
  // The pages returned from this are assumed to be used in the following ways.
  //  * Our VmObjectPaged backlink, or any of children's backlinks, are allowed to have readable
  //    mappings, and will be informed to unmap via the backlinks when needed.
  //  * Our VmObjectPaged backlink and our *slice* children are allowed to have writable mappings,
  //    and will be informed to either unmap or remove writability when needed.
  zx_status_t LookupPagesLocked(uint64_t offset, uint pf_flags, DirtyTrackingAction mark_dirty,
                                uint64_t max_out_pages, list_node* alloc_list,
                                LazyPageRequest* page_request, LookupInfo* out) TA_REQ(lock_);

  // Controls how the Add[New]Page[s]Locked functions handle the presence of already existing
  // non-empty slots in the page list, i.e. entries that return false for IsEmpty().
  enum class ExistingEntryAction : uint8_t {
    // Do not overwrite any non-empty slots, i.e. only populate empty slots.
    OverwriteNone,
    // Only overwrite slots that represent initial content. In the case of anonymous VMOs,
    // zero page markers represent initial content, as the entire VMO is implicitly zero on
    // creation. For pager backed VMOs, initial content is explicitly supplied by the pager. (See
    // comment in AddPageLocked for more details.)
    OverwriteInitialContent,
    // Overwrite any slots, regardless of the type of content, pages or markers.
    OverwriteAnyContent,
  };
  // Adds an allocated page to this cow pages at the specified offset, can be optionally zeroed and
  // any mappings invalidated. If an error is returned the caller retains ownership of |page|.
  // Offset must be page aligned.
  //
  // |overwrite| controls how the function handles a pre-existing non-empty slot at |offset|. If an
  // already existing page or marker is found and |overwrite| does not permit replacing it,
  // ZX_ERR_ALREADY_EXISTS will be returned. If a page is released from the page list as a result of
  // overwriting, it is returned through |released_page| and the caller takes ownership of this
  // page. If the |overwrite| action is such that a page cannot be released, it is valid for the
  // caller to pass in nullptr for |released_page|.
  zx_status_t AddNewPageLocked(uint64_t offset, vm_page_t* page, ExistingEntryAction overwrite,
                               ktl::optional<vm_page_t*>* released_page, bool zero = true,
                               bool do_range_update = true) TA_REQ(lock_);

  // Adds a set of pages consecutively starting from the given offset. Regardless of the return
  // result ownership of the pages is taken. Pages are assumed to be in the ALLOC state and can be
  // optionally zeroed before inserting. start_offset must be page aligned.
  //
  // |overwrite| controls how the function handles pre-existing non-empty slots in the range. If
  // an already existing page or marker is found and |overwrite| does not permit replacing it,
  // ZX_ERR_ALREADY_EXISTS will be returned. Pages released from the page list as a result of
  // overwriting are returned through |released_pages| and the caller takes ownership of these
  // pages. If the |overwrite| action is such that pages cannot be released, it is valid for the
  // caller to pass in nullptr for |released_pages|.
  zx_status_t AddNewPagesLocked(uint64_t start_offset, list_node_t* pages,
                                ExistingEntryAction overwrite, list_node_t* released_pages,
                                bool zero = true, bool do_range_update = true) TA_REQ(lock_);

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
  //
  // This method also replaces any loaned pages with non-loaned pages.
  zx_status_t PinRangeLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);

  // See VmObject::Unpin
  void UnpinLocked(uint64_t offset, uint64_t len, bool allow_gaps) TA_REQ(lock_);

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

  // Swap an old page for a new page.  The old page must be at offset.  The new page must be in
  // ALLOC state.  On return, the old_page is owned by the caller.  Typically the caller will
  // remove the old_page from pmm_page_queues() and free the old_page.
  void SwapPageLocked(uint64_t offset, vm_page_t* old_page, vm_page_t* new_page) TA_REQ(lock_);

  // If page is still at offset, replace it with a different page.  If with_loaned is true, replace
  // with a loaned page.  If with_loaned is false, replace with a non-loaned page.
  zx_status_t ReplacePage(vm_page_t* before_page, uint64_t offset, bool with_loaned) TA_EXCL(lock_);
  zx_status_t ReplacePageLocked(vm_page_t* before_page, uint64_t offset, bool with_loaned,
                                vm_page_t** after_page) TA_REQ(lock_);

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

  // VMO_VALIDATION
  bool DebugValidatePageSplitsLocked() const TA_REQ(lock_);
  bool DebugValidateBacklinksLocked() const TA_REQ(lock_);
  // Calls DebugValidatePageSplitsLocked on this and every parent in the chain, returning true if
  // all return true.  Also calls DebugValidateBacklinksLocked() on every node in the hierarchy.
  bool DebugValidatePageSplitsHierarchyLocked() const TA_REQ(lock_);

  // VMO_FRUGAL_VALIDATION
  bool DebugValidateVmoPageBorrowingLocked() const TA_REQ(lock_);

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

  void MarkAsLatencySensitiveLocked() TA_REQ(lock_);

  zx_status_t LockRangeLocked(uint64_t offset, uint64_t len, zx_vmo_lock_state_t* lock_state_out);
  zx_status_t TryLockRangeLocked(uint64_t offset, uint64_t len);
  zx_status_t UnlockRangeLocked(uint64_t offset, uint64_t len);

  // Exposed for testing.
  uint64_t DebugGetLockCount() const {
    Guard<Mutex> guard{&lock_};
    return lock_count_;
  }
  uint64_t DebugGetPageCountLocked() const TA_REQ(lock_);
  bool DebugIsReclaimable() const;
  bool DebugIsUnreclaimable() const;
  bool DebugIsDiscarded() const;
  bool DebugIsPage(uint64_t offset) const;
  bool DebugIsMarker(uint64_t offset) const;
  bool DebugIsEmpty(uint64_t offset) const;
  vm_page_t* DebugGetPage(uint64_t offset) const TA_EXCL(lock_);
  vm_page_t* DebugGetPageLocked(uint64_t offset) const TA_REQ(lock_);

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

  // Only for use by loaned page reclaim.
  VmCowPagesContainer* raw_container();

 private:
  // private constructor (use Create...())
  VmCowPages(ktl::unique_ptr<VmCowPagesContainer> cow_container,
             fbl::RefPtr<VmHierarchyState> root_lock, VmCowPagesOptions options,
             uint32_t pmm_alloc_flags, uint64_t size, fbl::RefPtr<PageSource> page_source);
  friend class VmCowPagesContainer;

  ~VmCowPages() override;

  // This takes all the constructor parameters including the VmCowPagesContainer, which avoids any
  // possiblity of allocation failure.
  template <class... Args>
  static fbl::RefPtr<VmCowPages> NewVmCowPages(ktl::unique_ptr<VmCowPagesContainer> cow_container,
                                               Args&&... args);

  // This takes all the constructor parameters except for the VmCowPagesContainer which is
  // allocated. The AllocChecker will reflect whether allocation was successful.
  template <class... Args>
  static fbl::RefPtr<VmCowPages> NewVmCowPages(fbl::AllocChecker* ac, Args&&... args);

  // fbl_recycle() does all the explicit cleanup, and the destructor does all the implicit cleanup.
  void fbl_recycle() override;
  friend class fbl::Recyclable<VmCowPages>;

  DISALLOW_COPY_ASSIGN_AND_MOVE(VmCowPages);

  bool is_hidden_locked() const TA_REQ(lock_) { return !!(options_ & VmCowPagesOptions::kHidden); }
  bool is_slice_locked() const TA_REQ(lock_) { return !!(options_ & VmCowPagesOptions::kSlice); }
  bool can_decommit_zero_pages_locked() const TA_REQ(lock_) {
    bool result = !(options_ & VmCowPagesOptions::kCannotDecommitZeroPages);
    DEBUG_ASSERT(result == !debug_is_contiguous());
    return result;
  }

  // can_borrow_locked() returns true if the VmCowPages is capable of borrowing pages, but whether
  // the VmCowPages should actually borrow pages also depends on a borrowing-site-specific flag that
  // the caller is responsible for checking (in addition to checking can_borrow_locked()).  Only if
  // both are true should the caller actually borrow at the caller's specific potential borrowing
  // site.  For example, see is_borrowing_in_supplypages_enabled() and
  // is_borrowing_on_mru_enabled().
  bool can_borrow_locked() const TA_REQ(lock_) {
    // TODO(dustingreen): Or rashaeqbal@.  We can only borrow while the page is not dirty.
    // Currently we enforce this by checking ShouldTrapDirtyTransitions() below and leaning on the
    // fact that !ShouldTrapDirtyTransitions() dirtying isn't implemented yet.  We currently evict
    // to reclaim instead of replacing the page, and we can't evict a dirty page since the contents
    // would be lost.  Option 1: When a loaned page is about to become dirty, we could replace it
    // with a non-loaned page.  Option 2: When reclaiming a loaned page we could replace instead of
    // evicting (this may be simpler).

    // Currently we can only borrow if we have a suitable PageSource, since this suitable page
    // source is currently 1:1 with having the needed backlinks for reclaim.
    bool source_is_suitable = page_source_ && page_source_->properties().is_preserving_page_content;
    // This ensures that if borrowing is globally disabled (no borrowing sites enabled), that we'll
    // return false.  We could delete this bool without damaging correctness, but we want to
    // mitigate a call site that maybe fails to check its call-site-specific settings such as
    // is_borrowing_in_supplypages_enabled().
    //
    // We also don't technically need to check is_any_borrowing_enabled() here since pmm will check
    // also, but by checking here, we minimize the amount of code that will run when
    // !is_any_borrowing_enabled() (in case we have it disabled due to late discovery of a problem
    // with borrowing).
    bool borrowing_is_generally_acceptable =
        pmm_physical_page_borrowing_config()->is_any_borrowing_enabled();
    // Exclude is_latency_sensitive_ to avoid adding latency due to reclaim.
    //
    // Currently we evict instead of replacing a page when reclaiming, so we want to avoid evicting
    // pages that are latency sensitive or are fairly likely to be pinned at some point.
    //
    // We also don't want to borrow a page that might get pinned again since we want to mitigate the
    // possibility of an invalid DMA-after-free.
    bool excluded_from_borrowing_for_latency_reasons = is_latency_sensitive_ || ever_pinned_;
    // Avoid borrowing and trapping dirty transitions overlapping for now; nothing really stops
    // these from being compatible AFAICT - we're just avoiding overlap of these two things until
    // later.
    bool overlapping_with_other_features = page_source_->ShouldTrapDirtyTransitions();

    bool result = source_is_suitable && borrowing_is_generally_acceptable &&
                  !excluded_from_borrowing_for_latency_reasons && !overlapping_with_other_features;

    DEBUG_ASSERT(result == (debug_is_user_pager_backed_locked() &&
                            pmm_physical_page_borrowing_config()->is_any_borrowing_enabled() &&
                            !is_latency_sensitive_ && !ever_pinned_ &&
                            !page_source_->ShouldTrapDirtyTransitions()));

    return result;
  }

  bool direct_source_supplies_zero_pages_locked() const TA_REQ(lock_) {
    bool result = page_source_ && !page_source_->properties().is_preserving_page_content;
    DEBUG_ASSERT(result == debug_is_contiguous());
    return result;
  }

  bool can_decommit_locked() const TA_REQ(lock_) {
    bool result = !page_source_ || !page_source_->properties().is_preserving_page_content;
    DEBUG_ASSERT(result == !debug_is_user_pager_backed_locked());
    return result;
  }

  // Add a page to the object at |offset|.
  //
  // |overwrite| controls how the function handles a pre-existing non-empty slot at |offset|. If an
  // already existing page or marker is found and |overwrite| does not permit replacing it,
  // ZX_ERR_ALREADY_EXISTS will be returned. If a page is released from the page list as a result of
  // overwriting, it is returned through |released_page| and the caller takes ownership of this
  // page. If the |overwrite| action is such that a page cannot be released, it is valid for the
  // caller to pass in nullptr for |released_page|.
  //
  // This operation unmaps the corresponding offset from any existing mappings, unless
  // |do_range_update| is false, in which case it will skip updating mappings.
  //
  // On success the page to add is moved out of `*p`, otherwise it is left there.
  zx_status_t AddPageLocked(VmPageOrMarker* p, uint64_t offset, ExistingEntryAction overwrite,
                            ktl::optional<vm_page_t*>* released_page, bool do_range_update = true)
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
  // this function successfully inserts a page into |offset| of the current vmo, it returns ZX_OK
  // and populates |out_page|. If a |page_request| is provided and ZX_ERR_SHOULD_WAIT is returned
  // then this indicates a transient failure that should be resolved by waiting on the page_request.
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
  zx_status_t CloneCowPageLocked(uint64_t offset, list_node_t* alloc_list, VmCowPages* page_owner,
                                 vm_page_t* page, uint64_t owner_offset,
                                 LazyPageRequest* page_request, vm_page_t** out_page) TA_REQ(lock_);

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
  void UnpinPageLocked(vm_page_t* page, uint64_t offset) TA_REQ(lock_);

  // Moves an existing page to the wired queue, retaining backlink information if applicable.
  void MoveToWiredLocked(vm_page_t* page, uint64_t offset) TA_REQ(lock_);

  // Updates the page queue of an existing page, moving it to whichever non wired queue
  // is appropriate.
  void MoveToNotWiredLocked(vm_page_t* page, uint64_t offset) TA_REQ(lock_);

  // Places a newly added page into the appropriate non wired page queue.
  void SetNotWiredLocked(vm_page_t* page, uint64_t offset) TA_REQ(lock_);

  // Updates any meta data for accessing a page. Currently this moves pager backed pages around in
  // the page queue to track which ones were recently accessed for the purposes of eviction. In
  // terms of functional correctness this never has to be called.
  void UpdateOnAccessLocked(vm_page_t* page, uint pf_flags) TA_REQ(lock_);

  // Prepares the specified range for a write, forwarding a DIRTY page request to the page source if
  // pages are clean and need to transition to dirty, in which case ZX_ERR_SHOULD_WAIT will be
  // returned and the caller should wait on |page_request|. If no page requests need to be
  // generated, i.e. the pages are already dirty, or if they do not require the dirty transition to
  // be trapped, ZX_OK is returned.
  //
  // |offset| and |len| should be page-aligned.
  //
  // |dirty_len_out| will return the (page-aligned) length starting at |offset| that contains dirty
  // pages, either already dirty before making the call or dirtied during the call.
  zx_status_t PrepareForWriteLocked(LazyPageRequest* page_request, uint64_t offset, uint64_t len,
                                    uint64_t* dirty_len_out) TA_REQ(lock_);

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

  void FreePages(list_node* pages) {
    if (!page_source_ || !page_source_->properties().is_handling_free) {
      pmm_free(pages);
      return;
    }
    page_source_->FreePages(pages);
  }

  void FreePage(vm_page_t* page) {
    DEBUG_ASSERT(!list_in_list(&page->queue_node));
    if (!page_source_ || !page_source_->properties().is_handling_free) {
      pmm_free_page(page);
      return;
    }
    list_node_t list;
    list_initialize(&list);
    list_add_tail(&list, &page->queue_node);
    page_source_->FreePages(&list);
  }

  void CopyPageForReplacementLocked(vm_page_t* dst_page, vm_page_t* src_page) TA_REQ(lock_);

  // magic value
  fbl::Canary<fbl::magic("VMCP")> canary_;

  // VmCowPages keeps this ref on VmCowPagesContainer until the end of VmCowPages::fbl_recycle().
  // This allows loaned page reclaim to upgrade a raw container pointer until _after_ all the pages
  // have been removed from the VmCowPages.  This way there's always something for loaned page
  // reclaim to block on that'll do priority inheritance to the thread that needs to finish moving
  // pages.
  fbl::RefPtr<VmCowPagesContainer> container_;
  VmCowPagesContainer* debug_retained_raw_container_ = nullptr;

  VmCowPagesOptions options_ TA_GUARDED(lock_);

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

  // TODO(fxb/85056): This is a temporary solution and needs to be replaced with something that is
  // formalized.
  // Marks whether or not this VMO is considered a latency sensitive object. For a VMO being latency
  // sensitive means pages that get committed should not be decommitted (or made expensive to
  // access) by any background kernel process, such as the zero page deduper.
  // Note: This does not presently protect against user pager eviction, as there is already a
  // separate mechanism for that. Once fxb/85056 is resolved this might change.
  bool is_latency_sensitive_ TA_GUARDED(lock_) = false;

  using Cursor =
      VmoCursor<VmCowPages, DiscardableVmosLock, DiscardableList, DiscardableList::iterator>;

  // The list of all outstanding cursors iterating over the discardable lists:
  // |discardable_reclaim_candidates_| and |discardable_non_reclaim_candidates_|. The cursors should
  // be advanced (by calling AdvanceIf()) before removing any element from the discardable lists.
  static fbl::DoublyLinkedList<Cursor*> discardable_vmos_cursors_
      TA_GUARDED(DiscardableVmosLock::Get());

  // With this bool we achieve these things:
  //  * Avoid using loaned pages for a VMO that will just get pinned and replace the loaned pages
  //    with non-loaned pages again, possibly repeatedly.
  //  * Avoid increasing pin latency in the (more) common case of pinning a VMO the 2nd or
  //    subsequent times (vs the 1st time).
  //  * Once we have any form of active sweeping (of data from non-loaned to loaned physical pages)
  //    this bool is part of mitigating any potential DMA-while-not-pinned (which is not permitted
  //    but is also difficult to detect or prevent without an IOMMU).
  bool ever_pinned_ TA_GUARDED(lock_) = false;
};

// VmCowPagesContainer exists to essentially split the VmCowPages ref_count_ into two counts, so
// that it remains possible to upgrade from a raw container pointer until after the VmCowPages
// fbl_recycle() has mostly completed and has removed and freed all the pages.
//
// This way, if we can upgrade, then we can call RemovePageForEviction() and it'll either work or
// the page will already have been removed from that location in the VmCowPages, or we can't
// upgrade, in which case all the pages have already been removed and freed.
//
// In contrast if we were to attempt upgrade of a raw VmCowPages pointer to VmCowPages ref, the
// ability to upgrade would disappear before the backlink is removed to make room for a
// StackOwnedLoanedPagesInterval, so loaned page reclaim would need to wait (somehow) for the page
// to be removed from the VmCowPages and at least have a backlink.  That wait is problematic since
// it would also need to propagate priority inheritance properly like StackOwnedLoanedPagesInterval
// does, but the interval begins at the moment the refcount goes from 1 to 0, and reliably wrapping
// that 1 to 0 transition, while definitely posssible with some RefPtr changes etc etc, is more
// complicated than having a VmCowPagesContainer whose ref can still be obtained up until after the
// pages have become FREE.  There may of course be yet other options that are overall better; please
// suggest if you think of one.
//
// All the explicit cleanup of VmCowPages happens in VmCowPages::fbl_recycle(), with the final
// explicit fbl_recycle() step being release of the containing VmCowPagesContainer which in turn
// triggers ~VmCowPages which finishes up with implicit cleanup of VmCowPages (but possibly delayed
// slightly by loaned page reclaimer(s) that can have a VmCowPagesContainer ref transiently).
//
// Those paying close attention may note that under high load with potential low priority thread
// starvation (with a hypothetical scheduling policy that is assumed to let thread starvation be
// possible), each low priority loaned page reclaiming thread may essentially be thought of as
// having up to one VmCowPagesContainer + contained de-populated VmCowPages as additional memory
// overhead that can be thought of as being essentially attributed to the memory cost of the low
// priority thread.  I think this is completely fine and completely analogous to many other similar
// situations.  In a sense it's priority inversion of the rest of cleanup of the VmCowPages memory,
// but since it's a depopulated VmCowPages, the symptom isn't enough of a problem to justify any
// mitigation other than mentally accounting for it in the low priority thread's memory cost.  We
// should be careful not to let a refcount held by a lower priority thread potentially keep
// unbounded memory allocated of course, but in this case it's well bounded.
//
// We restrict visibility of VmCowPages via its VmCowPagesContainer, to control which methods are
// ok to call on the VmCowPages via a VmCowPagesContainer ref while lacking any direct VmCowPages
// ref.  The methods that are ok to call with only a VmCowPagesContainer ref are called via a
// corresponding method on VmCowPagesContainer.
class VmCowPagesContainer : public fbl::RefCountedUpgradeable<VmCowPagesContainer> {
 public:
  VmCowPagesContainer() = default;
  ~VmCowPagesContainer();

  // These are the only VmCowPages methods that are ok to call via ref on VmCowPagesContainer while
  // holding no ref on the contained VmCowPages.  These will operate correctly despite potential
  // concurrent VmCowPages::fbl_recycle() on a different thread and despite VmCowPages refcount_
  // potentially being 0.  The VmCowPagesContainer ref held by the caller keeps the actual
  // VmCowPages object alive during this call.
  bool RemovePageForEviction(vm_page_t* page, uint64_t offset,
                             VmCowPages::EvictionHintAction hint_action);

  zx_status_t ReplacePage(vm_page_t* page, uint64_t offset, bool with_loaned);

 private:
  friend class VmCowPages;

  // We'd use ktl::optional<VmCowPages> or std::variant<monostate, VmCowPages>, but both those
  // require is_constructible_v<VmCowPages, ...>, which in turn requires the VmCowPages constructor
  // to be public, which we don't want.

  // Used for construction of contained VmCowPages.
  template <class... Args>
  void EmplaceCow(Args&&... args);

  VmCowPages& cow();

  ktl::aligned_storage_t<sizeof(VmCowPages), alignof(VmCowPages)> cow_space_;
  bool is_cow_present_ = false;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_VM_COW_PAGES_H_
