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

// Implements a copy-on-write hierarchy of pages in a VmPageList.
class VmCowPages final : public VmHierarchyBase,
                         public fbl::ContainableBaseClasses<
                             fbl::TaggedDoublyLinkedListable<VmCowPages*, internal::ChildListTag>> {
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

  fbl::RefPtr<PageSource> GetRootPageSourceLocked() const TA_REQ(lock_);

  void DetachSource() {
    DEBUG_ASSERT(page_source_);
    page_source_->Detach();
  }

  // Resizes the range of this cow pages. |size| must be a multiple of the page size and this must
  // not be called on slices or nodes with slice children.
  zx_status_t ResizeLocked(uint64_t size) TA_REQ(lock_);

  // See VmObject::Lookup
  zx_status_t LookupLocked(uint64_t offset, uint64_t len, vmo_lookup_fn_t lookup_fn, void* context)
      TA_REQ(lock_);

  // See VmObject::TakePages
  zx_status_t TakePagesLocked(uint64_t offset, uint64_t len, VmPageSpliceList* pages) TA_REQ(lock_);

  // See VmObject::SupplyPages
  zx_status_t SupplyPagesLocked(uint64_t offset, uint64_t len, VmPageSpliceList* pages)
      TA_REQ(lock_);

  // See VmObject::FailPageRequests
  zx_status_t FailPageRequestsLocked(uint64_t offset, uint64_t len, zx_status_t error_status)
      TA_REQ(lock_);

  // See VmObject::GetPageLocked
  // The pages returned from this are assumed to be used in the following ways.
  //  * Our VmObjectPaged backlink, or any of childrens backlinks, are allowed to have readable
  //    mappings, and will be informed to unmap via the backlinks when needed.
  //  * Our VmObjectPaged backlink and our *slice* children are allowed to have writable mappings,
  //    and will be informed to either unmap or remove writability when needed.
  zx_status_t GetPageLocked(uint64_t offset, uint pf_flags, list_node* free_list,
                            PageRequest* page_request, vm_page_t**, paddr_t*) TA_REQ(lock_);

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
                                PageRequest* page_request) TA_REQ(lock_);

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

  // See VmObject::ScanForZeroPages
  uint32_t ScanForZeroPagesLocked(bool reclaim) TA_REQ(lock_);

  // Asks the VMO to attempt to evict the specified page. This returns true if the page was
  // actually from this VMO and was successfully evicted, at which point the caller now has
  // ownership of the page. Otherwise eviction is allowed to fail for any reason, specifically
  // if the page is considered in use, or the VMO has no way to recreate the page then eviction
  // will fail. Although eviction may fail for any reason, if it does the caller is able to assume
  // that either the page was not from this vmo, or that the page is not in any evictable page queue
  // (such as the pager_backed_ queue).
  bool EvictPage(vm_page_t* page, uint64_t offset);

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

  // Different operations that RangeChangeUpdate* can perform against any VmMappings that are found.
  enum class RangeChangeOp {
    Unmap,
    RemoveWrite,
  };
  // Apply the specified operation to all mappings in the given range. This is applied to all
  // descendants within the range.
  void RangeChangeUpdateLocked(uint64_t offset, uint64_t len, RangeChangeOp op) TA_REQ(lock_);

 private:
  // private constructor (use Create())
  VmCowPages(fbl::RefPtr<VmHierarchyState> root_lock, uint32_t options, uint32_t pmm_alloc_flags,
             uint64_t size, fbl::RefPtr<PageSource> page_source);

  // private destructor, only called from refptr
  ~VmCowPages() override;
  friend fbl::RefPtr<VmCowPages>;

  DISALLOW_COPY_ASSIGN_AND_MOVE(VmCowPages);

  zx_status_t CreateHidden(fbl::RefPtr<VmCowPages>* hidden_cow);

  bool is_hidden_locked() const TA_REQ(lock_) { return (options_ & kHidden); }
  bool is_slice_locked() const TA_REQ(lock_) { return options_ & kSlice; }

  // Add a page to the object. This operation unmaps the corresponding
  // offset from any existing mappings.
  // If |do_range_update| is false, this function will skip updating mappings.
  // On success the page to add is moved out of `*p`, otherwise it is left there.
  zx_status_t AddPageLocked(VmPageOrMarker* p, uint64_t offset, bool do_range_update = true)
      TA_REQ(lock_);

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
  // as well as the VmObjectPaged and offset which own the page. If no ancestor has a committed
  // page for the offset, returns null as well as the VmObjectPaged/offset which need to be queried
  // to populate the page.
  VmPageOrMarker* FindInitialPageContentLocked(uint64_t offset, VmCowPages** owner_out,
                                               uint64_t* owner_offset_out) TA_REQ(lock_);

  // GetPageLocked helper function that 'forks' the page at |offset| of the current vmo. If
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
  vm_page_t* CloneCowPageLocked(uint64_t offset, list_node_t* free_list, VmCowPages* page_owner,
                                vm_page_t* page, uint64_t owner_offset) TA_REQ(lock_);

  // This is an optimized wrapper around CloneCowPageLocked for when an initial content page needs
  // to be forked to preserve the COW invariant, but you know you are immediately going to overwrite
  // the forked page with zeros.
  //
  // The optimization it can make is that it can fork the page up to the parent and then, instead
  // of forking here and then having to immediately free the page, it can insert a marker here and
  // set the split bits in the parent page as if it had been forked.
  zx_status_t CloneCowPageAsZeroLocked(uint64_t offset, list_node_t* free_list,
                                       VmCowPages* page_owner, vm_page_t* page,
                                       uint64_t owner_offset) TA_REQ(lock_);

  // Returns true if |page| (located at |offset| in this vmo) is only accessible by one
  // child, where 'accessible' is defined by ::CloneCowPageLocked.
  bool IsUniAccessibleLocked(vm_page_t* page, uint64_t offset) const TA_REQ(lock_);

  // Releases this vmo's reference to any ancestor vmo's COW pages, for the range [start, end)
  // in this vmo. This is done by either setting the pages' split bits (if something else
  // can access the pages) or by freeing the pages onto |free_list| (if nothing else can
  // access the pages).
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
  void UpdateOnAccessLocked(vm_page_t* page, uint64_t offset) TA_REQ(lock_);

  // Inserts |hidden_parent| as a hidden parent of |this|. This vmo and |hidden_parent|
  // must have the same lock.
  void InsertHiddenParentLocked(fbl::RefPtr<VmCowPages> hidden_parent) TA_REQ(lock_);

  // Initializes the original parent state of the vmo. |offset| is the offset of
  // this vmo in |parent|.
  //
  // This function should be called at most once, even if the parent changes
  // after initialization.
  void InitializeOriginalParentLocked(fbl::RefPtr<VmCowPages> parent, uint64_t offset)
      TA_REQ(lock_);

  void RemoveChildLocked(VmCowPages* child) TA_REQ(lock_);
  void AddChildLocked(VmCowPages* child) TA_REQ(lock_);

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

  // a tree of pages
  VmPageList page_list_ TA_GUARDED(lock_);

  RangeChangeNodeState range_change_state_;
  uint64_t range_change_offset_ TA_GUARDED(lock_);
  uint64_t range_change_len_ TA_GUARDED(lock_);

  // optional reference back to a VmObjectPaged so that we can perform mapping updates. This is a
  // raw pointer to avoid circular references, the VmObjectPaged destructor needs to update it.
  VmObjectPaged* paged_ref_ TA_GUARDED(lock_) = nullptr;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_VM_COW_PAGES_H_
