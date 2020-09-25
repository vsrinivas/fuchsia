// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_VM_OBJECT_PAGED_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_VM_OBJECT_PAGED_H_

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

// Forward declare this so VmObjectPaged helpers can accept references.
class BatchPQRemove;

// the main VM object type, holding a list of pages
class VmObjectPaged final : public VmObject {
 public:
  // |options_| is a bitmask of:
  static constexpr uint32_t kResizable = (1u << 0);
  static constexpr uint32_t kContiguous = (1u << 1);
  static constexpr uint32_t kHidden = (1u << 2);
  static constexpr uint32_t kSlice = (1u << 3);

  static zx_status_t Create(uint32_t pmm_alloc_flags, uint32_t options, uint64_t size,
                            fbl::RefPtr<VmObjectPaged>* vmo);

  // Gets the raw VmObjectPaged pointer, or null if the VmObject is not paged.
  static VmObjectPaged* AsVmObjectPaged(const fbl::RefPtr<VmObject>& vmo) {
    if (vmo->is_paged()) {
      return static_cast<VmObjectPaged*>(vmo.get());
    } else {
      return nullptr;
    }
  }

  // Create a VMO backed by a contiguous range of physical memory.  The
  // returned vmo has all of its pages committed, and does not allow
  // decommitting them.
  static zx_status_t CreateContiguous(uint32_t pmm_alloc_flags, uint64_t size,
                                      uint8_t alignment_log2, fbl::RefPtr<VmObjectPaged>* vmo);

  // Creates a VMO from wired pages.
  //
  // Creating a VMO using this method is destructive. Once the VMO is released, its
  // pages will be released into the general purpose page pool, so it is not possible
  // to create multiple VMOs for the same region using this method.
  //
  // |exclusive| indicates whether or not the created vmo should have exclusive access to
  // the pages. If exclusive is true, then [data, data + size) will be unmapped from the
  // kernel address space (unless they lie in the physmap).
  static zx_status_t CreateFromWiredPages(const void* data, size_t size, bool exclusive,
                                          fbl::RefPtr<VmObjectPaged>* vmo);

  static zx_status_t CreateExternal(fbl::RefPtr<PageSource> src, uint32_t options, uint64_t size,
                                    fbl::RefPtr<VmObjectPaged>* vmo);

  zx_status_t Resize(uint64_t size) override;
  uint32_t create_options() const override { return options_; }
  uint64_t size() const override
      // TODO: Figure out whether it's safe to lock here without causing
      // any deadlocks.
      TA_NO_THREAD_SAFETY_ANALYSIS {
    return size_;
  }
  bool is_paged() const override { return true; }
  bool is_contiguous() const override { return (options_ & kContiguous); }
  bool is_resizable() const override { return (options_ & kResizable); }
  bool is_pager_backed() const override {
    Guard<Mutex> guard{&lock_};
    return GetRootPageSourceLocked() != nullptr;
  }
  bool is_hidden() const override { return (options_ & kHidden); }
  ChildType child_type() const override {
    if (is_slice()) {
      return ChildType::kSlice;
    }
    Guard<Mutex> guard{&lock_};
    return (original_parent_user_id_ != 0) ? ChildType::kCowClone : ChildType::kNotChild;
  }
  bool is_slice() const { return options_ & kSlice; }
  uint64_t parent_user_id() const override {
    Guard<Mutex> guard{&lock_};
    return original_parent_user_id_;
  }
  void set_user_id(uint64_t user_id) override {
    VmObject::set_user_id(user_id);
    Guard<Mutex> guard{&lock_};
    page_attribution_user_id_ = user_id;
  }

  uint64_t HeapAllocationBytes() const override {
    Guard<Mutex> guard{&lock_};
    return page_list_.HeapAllocationBytes();
  }

  uint64_t EvictedPagedCount() const override {
    Guard<Mutex> guard{&lock_};
    return eviction_event_count_;
  }

  size_t AttributedPagesInRange(uint64_t offset, uint64_t len) const override;

  zx_status_t CommitRange(uint64_t offset, uint64_t len) override {
    Guard<Mutex> guard{&lock_};
    return CommitRangeInternal(offset, len, false, guard.take());
  }
  zx_status_t CommitRangePinned(uint64_t offset, uint64_t len) override {
    Guard<Mutex> guard{&lock_};
    return CommitRangeInternal(offset, len, true, guard.take());
  }
  zx_status_t DecommitRange(uint64_t offset, uint64_t len) override;
  zx_status_t ZeroRange(uint64_t offset, uint64_t len) override;

  void Unpin(uint64_t offset, uint64_t len) override;

  zx_status_t Read(void* ptr, uint64_t offset, size_t len) override;
  zx_status_t Write(const void* ptr, uint64_t offset, size_t len) override;
  zx_status_t Lookup(uint64_t offset, uint64_t len, vmo_lookup_fn_t lookup_fn,
                     void* context) override;
  zx_status_t LookupContiguous(uint64_t offset, uint64_t len, paddr_t* out_paddr) override;

  zx_status_t ReadUser(VmAspace* current_aspace, user_out_ptr<char> ptr, uint64_t offset,
                       size_t len) override;
  zx_status_t WriteUser(VmAspace* current_aspace, user_in_ptr<const char> ptr, uint64_t offset,
                        size_t len) override;

  zx_status_t TakePages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) override;
  zx_status_t SupplyPages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) override;
  zx_status_t FailPageRequests(uint64_t offset, uint64_t len, zx_status_t error_status) override;

  void Dump(uint depth, bool verbose) override {
    Guard<Mutex> guard{&lock_};
    DumpLocked(depth, verbose);
  }

  zx_status_t GetPageLocked(uint64_t offset, uint pf_flags, list_node* free_list,
                            PageRequest* page_request, vm_page_t**, paddr_t*) override
      TA_REQ(lock_);

  zx_status_t CreateClone(Resizability resizable, CloneType type, uint64_t offset, uint64_t size,
                          bool copy_name, fbl::RefPtr<VmObject>* child_vmo) override;
  // Inserts |hidden_parent| as a hidden parent of |this|. This vmo and |hidden_parent|
  // must have the same lock.
  void InsertHiddenParentLocked(fbl::RefPtr<VmObjectPaged>&& hidden_parent) TA_REQ(lock_);

  uint32_t GetMappingCachePolicy() const override;
  zx_status_t SetMappingCachePolicy(const uint32_t cache_policy) override;

  void RemoveChild(VmObject* child, Guard<Mutex>&& guard) override TA_REQ(lock_);
  bool OnChildAddedLocked() override TA_REQ(lock_);

  void DetachSource() override {
    DEBUG_ASSERT(page_source_);
    page_source_->Detach();
  }

  zx_status_t CreateChildSlice(uint64_t offset, uint64_t size, bool copy_name,
                               fbl::RefPtr<VmObject>* child_vmo) override;

  uint32_t ScanForZeroPages(bool reclaim) override;

  bool EvictPage(vm_page_t* page, uint64_t offset) override;
  void HarvestAccessedBits() override;

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

  // This performs a very expensive validation that checks if pages have been split correctly in
  // this VMO and is intended as a debugging aid. A return value of false indicates that the VMO
  // hierarchy is corrupt and the system should probably panic as soon as possible. As a result,
  // if false is returned this may write various additional information to the debuglog.
  bool DebugValidatePageSplits() const {
    Guard<Mutex> guard{&lock_};
    return DebugValidatePageSplitsLocked();
  }

  // Used to cache the page attribution count for this VMO. Also tracks the hierarchy generation
  // count at the time of caching the attributed page count.
  struct CachedPageAttribution {
    uint32_t generation_count = kGenerationCountUnset;
    size_t page_count = 0;
  };

  // Exposed for testing.
  CachedPageAttribution GetCachedPageAttribution() const {
    Guard<Mutex> guard{&lock_};
    return cached_page_attribution_;
  }

  // Exposed for testing.
  uint32_t GetHierarchyGenerationCount() const {
    Guard<Mutex> guard{&lock_};
    return GetHierarchyGenerationCountLocked();
  }

 private:
  // private constructor (use Create())
  VmObjectPaged(uint32_t options, uint32_t pmm_alloc_flags, uint64_t size,
                fbl::RefPtr<vm_lock_t> root_lock, fbl::RefPtr<PageSource> page_source);

  // Initializes the original parent state of the vmo. |offset| is the offset of
  // this vmo in |parent|.
  //
  // This function should be called at most once, even if the parent changes
  // after initialization.
  void InitializeOriginalParentLocked(fbl::RefPtr<VmObjectPaged> parent, uint64_t offset)
      TA_REQ(lock_);

  static zx_status_t CreateCommon(uint32_t pmm_alloc_flags, uint32_t options, uint64_t size,
                                  fbl::RefPtr<VmObjectPaged>* vmo);

  // private destructor, only called from refptr
  ~VmObjectPaged() override;
  friend fbl::RefPtr<VmObjectPaged>;

  DISALLOW_COPY_ASSIGN_AND_MOVE(VmObjectPaged);

  // Add a page to the object. This operation unmaps the corresponding
  // offset from any existing mappings.
  zx_status_t AddPage(vm_page_t* p, uint64_t offset);
  // If |do_range_update| is false, this function will skip updating mappings.
  // On success the page to add is moved out of `*p`, otherwise it is left there.
  zx_status_t AddPageLocked(VmPageOrMarker* p, uint64_t offset, bool do_range_update = true)
      TA_REQ(lock_);

  // internal page list routine
  void AddPageToArray(size_t index, vm_page_t* p);

  // Unified function that implements both CommitRange and CommitRangePinned
  zx_status_t CommitRangeInternal(uint64_t offset, uint64_t len, bool pin, Guard<Mutex>&& adopt);

  void UnpinLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);

  // Internal decommit range helper that expects the lock to be held. On success it will populate
  // the past in page list with any pages that should be freed.
  zx_status_t DecommitRangeLocked(uint64_t offset, uint64_t len, list_node_t& free_list)
      TA_REQ(lock_);
  zx_status_t ZeroRangeLocked(uint64_t offset, uint64_t len, list_node_t* free_list,
                              Guard<Mutex>* guard) TA_REQ(lock_);

  fbl::RefPtr<PageSource> GetRootPageSourceLocked() const TA_REQ(lock_);

  bool IsCowClonableLocked() const TA_REQ(lock_);

  // internal check if any pages in a range are pinned
  bool AnyPagesPinnedLocked(uint64_t offset, size_t len) TA_REQ(lock_);

  // Get the current generation count of the VMO hierarchy this VMO is a part of. Walks up the VMO
  // tree to the root.
  uint32_t GetHierarchyGenerationCountLocked() const TA_REQ(lock_);

  // Increment the generation count of the VMO hierarchy this VMO is a part of. Walks up the VMO
  // tree to the root.
  //
  // This should be called whenever a change is made to the VMO tree or the VMO's page list, that
  // could result in page attribution counts to change for any VMO in this tree.
  void IncrementHierarchyGenerationCountLocked() TA_REQ(lock_);

  // see AttributedPagesInRange
  size_t AttributedPagesInRangeLocked(uint64_t offset, uint64_t len) const TA_REQ(lock_);
  // Helper function for ::AllocatedPagesInRangeLocked. Counts the number of pages in ancestor's
  // vmos that should be attributed to this vmo for the specified range. It is an error to pass in a
  // range that does not need attributing (i.e. offset must be < parent_limit_), although |len| is
  // permitted to be sized such that the range exceeds parent_limit_.
  // The return value is the length of the processed region, which will be <= |size| and is
  // guaranteed to be > 0. The |count| is the number of pages in this region that should be
  // attributed to this vmo, versus some other vmo.
  uint64_t CountAttributedAncestorPagesLocked(uint64_t offset, uint64_t size, uint64_t* count) const
      TA_REQ(lock_);

  // internal read/write routine that takes a templated copy function to help share some code
  template <typename T>
  zx_status_t ReadWriteInternalLocked(uint64_t offset, size_t len, bool write, T copyfunc,
                                      Guard<Mutex>* guard) TA_REQ(lock_);

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
  VmPageOrMarker* FindInitialPageContentLocked(uint64_t offset, VmObjectPaged** owner_out,
                                               uint64_t* owner_offset_out, uint64_t* owner_id_out)
      TA_REQ(lock_);

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
  vm_page_t* CloneCowPageLocked(uint64_t offset, list_node_t* free_list, VmObjectPaged* page_owner,
                                vm_page_t* page, uint64_t owner_offset) TA_REQ(lock_);

  // This is an optimized wrapper around CloneCowPageLocked for when an initial content page needs
  // to be forked to preserve the COW invariant, but you know you are immediately going to overwrite
  // the forked page with zeros.
  //
  // The optimization it can make is that it can fork the page up to the parent and then, instead
  // of forking here and then having to immediately free the page, it can insert a marker here and
  // set the split bits in the parent page as if it had been forked.
  zx_status_t CloneCowPageAsZeroLocked(uint64_t offset, list_node_t* free_list,
                                       VmObjectPaged* page_owner, vm_page_t* page,
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
  void MergeContentWithChildLocked(VmObjectPaged* removed, bool removed_left) TA_REQ(lock_);

  // Only valid to be called when is_slice() is true and returns the first parent of this
  // hierarchy that is not a slice. The offset of this slice within that VmObjectPaged is set as
  // the output.
  VmObjectPaged* PagedParentOfSliceLocked(uint64_t* offset) TA_REQ(lock_);

  // Zeroes a partial range in a page. May use CallUnlocked on the passed in guard. The page to zero
  // is looked up using page_base_offset, and will be committed if needed. The range of
  // [zero_start_offset, zero_end_offset) is relative to the page and so [0, PAGE_SIZE) would zero
  // the entire page.
  zx_status_t ZeroPartialPage(uint64_t page_base_offset, uint64_t zero_start_offset,
                              uint64_t zero_end_offset, Guard<Mutex>* guard) TA_REQ(lock_);

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

  // Outside of initialization/destruction, hidden vmos always have two children. For
  // clarity, whichever child is first in the list is the 'left' child, and whichever
  // child is second is the 'right' child. Children of a paged vmo will always be paged
  // vmos themselves.
  VmObjectPaged& left_child_locked() TA_REQ(lock_) TA_ASSERT(left_child_locked().lock()) {
    DEBUG_ASSERT(is_hidden());
    DEBUG_ASSERT(children_list_len_ == 2);
    DEBUG_ASSERT(children_list_.front().is_paged());

    auto& ret = static_cast<VmObjectPaged&>(children_list_.front());
    AssertHeld(ret.lock_);
    return ret;
  }
  VmObjectPaged& right_child_locked() TA_REQ(lock_) TA_ASSERT(right_child_locked().lock()) {
    DEBUG_ASSERT(is_hidden());
    DEBUG_ASSERT(children_list_len_ == 2);
    DEBUG_ASSERT(children_list_.back().is_paged());
    auto& ret = static_cast<VmObjectPaged&>(children_list_.back());
    AssertHeld(ret.lock_);
    return ret;
  }
  const VmObjectPaged& left_child_locked() const TA_REQ(lock_)
      TA_ASSERT(left_child_locked().lock()) {
    DEBUG_ASSERT(is_hidden());
    DEBUG_ASSERT(children_list_len_ == 2);
    DEBUG_ASSERT(children_list_.front().is_paged());
    const auto& ret = static_cast<const VmObjectPaged&>(children_list_.front());
    AssertHeld(ret.lock_);
    return ret;
  }
  const VmObjectPaged& right_child_locked() const TA_REQ(lock_)
      TA_ASSERT(right_child_locked().lock()) {
    DEBUG_ASSERT(is_hidden());
    DEBUG_ASSERT(children_list_len_ == 2);
    DEBUG_ASSERT(children_list_.back().is_paged());
    const auto& ret = static_cast<const VmObjectPaged&>(children_list_.back());
    AssertHeld(ret.lock_);
    return ret;
  }

  // Internal implementations that assume lock is already held.
  void DumpLocked(uint depth, bool verbose) const TA_REQ(lock_);
  bool DebugValidatePageSplitsLocked() const TA_REQ(lock_);

  // Different operations that RangeChangeUpdate* can perform against any VmMappings that are found.
  enum class RangeChangeOp {
    Unmap,
    RemoveWrite,
  };

  // Types for an additional linked list over the VmObjectPaged for use when doing a
  // RangeChangeUpdate.
  //
  // To avoid unbounded stack growth we need to reserve the memory to exist on a
  // RangeChange list in our object so that we can have a flat iteration over a
  // work list. RangeChangeLists should only be used by the RangeChangeUpdate
  // code.
  using RangeChangeNodeState = fbl::SinglyLinkedListNodeState<VmObjectPaged*>;
  struct RangeChangeTraits {
    static RangeChangeNodeState& node_state(VmObjectPaged& foo) { return foo.range_change_state_; }
  };
  using RangeChangeList =
      fbl::SinglyLinkedListCustomTraits<VmObjectPaged*, VmObjectPaged::RangeChangeTraits>;
  friend struct RangeChangeTraits;

  // Apply the specified operation to all mappings in the given range. This is applied to all
  // descendants within the range.
  void RangeChangeUpdateLocked(uint64_t offset, uint64_t len, RangeChangeOp op) TA_REQ(lock_);

  // Given an initial list of VmObject's performs RangeChangeUpdate on it until the list is empty.
  static void RangeChangeUpdateListLocked(RangeChangeList* list, RangeChangeOp op);

  void RangeChangeUpdateFromParentLocked(uint64_t offset, uint64_t len, RangeChangeList* list)
      TA_REQ(lock_);

  // members
  const uint32_t options_;
  uint64_t size_ TA_GUARDED(lock_) = 0;
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
  const uint32_t pmm_alloc_flags_ = PMM_ALLOC_FLAG_ANY;
  uint32_t cache_policy_ TA_GUARDED(lock_) = ARCH_MMU_FLAG_CACHED;

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
  fbl::RefPtr<VmObjectPaged> parent_ TA_GUARDED(lock_);
  // Record the user_id_ of the original parent, in case we make
  // a bidirectional clone and end up changing parent_.
  uint64_t original_parent_user_id_ TA_GUARDED(lock_) = 0;

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

  static constexpr uint32_t kGenerationCountUnset = 0;
  static constexpr uint32_t kGenerationCountInitial = 1;

  // Each VMO hierarchy has a generation count, which is incremented on any change to the hierarchy
  // - either in the VMO tree, or the page lists of VMO's. The root of the VMO tree owns the
  // generation count for the hierarchy, every other VMO in the tree has its generation count set to
  // |kGenerationCountInitial|. We move the generation count up and down the tree (to the current
  // root) as required, as clones and hidden parents come and go.
  //
  // The generation count is used to implement caching for page attribution counts, which get
  // queried frequently to periodically track memory usage on the system. Attributing pages to a
  // VMO is an expensive operation and involves walking the VMO tree, quite often multiple times.
  // If the generation count does not change between two successive queries, we can avoid
  // re-counting attributed pages, and simply return the previously cached value.
  uint32_t hierarchy_generation_count_ TA_GUARDED(lock_) = kGenerationCountInitial;

  // Tracks the last cached page attribution count.
  mutable CachedPageAttribution cached_page_attribution_ TA_GUARDED(lock_) = {};

  // Counts the total number of pages pinned by ::Pin. If one page is pinned n times, it
  // contributes n to this count. However, this does not include pages pinned when creating
  // a contiguous vmo.
  uint64_t pinned_page_count_ TA_GUARDED(lock_) = 0;

  // Count eviction events so that we can report them to the user.
  uint64_t eviction_event_count_ TA_GUARDED(lock_) = 0;

  // The page source, if any.
  const fbl::RefPtr<PageSource> page_source_;

  RangeChangeNodeState range_change_state_;
  uint64_t range_change_offset_ TA_GUARDED(lock_);
  uint64_t range_change_len_ TA_GUARDED(lock_);

  // a tree of pages
  VmPageList page_list_ TA_GUARDED(lock_);
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_VM_OBJECT_PAGED_H_
