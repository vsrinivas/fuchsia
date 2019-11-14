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
#include <list.h>
#include <stdint.h>
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

// the main VM object type, holding a list of pages
class VmObjectPaged final : public VmObject {
 public:
  // |options_| is a bitmask of:
  static constexpr uint32_t kResizable = (1u << 0);
  static constexpr uint32_t kContiguous = (1u << 1);
  static constexpr uint32_t kHidden = (1u << 2);
  static constexpr uint32_t kSlice = (1u << 3);

  static zx_status_t Create(uint32_t pmm_alloc_flags, uint32_t options, uint64_t size,
                            fbl::RefPtr<VmObject>* vmo);

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
                                      uint8_t alignment_log2, fbl::RefPtr<VmObject>* vmo);

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
                                          fbl::RefPtr<VmObject>* vmo);

  static zx_status_t CreateExternal(fbl::RefPtr<PageSource> src, uint32_t options, uint64_t size,
                                    fbl::RefPtr<VmObject>* vmo);

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
    Guard<fbl::Mutex> guard{&lock_};
    return GetRootPageSourceLocked() != nullptr;
  }
  bool is_hidden() const override { return (options_ & kHidden); }
  ChildType child_type() const override {
    Guard<fbl::Mutex> guard{&lock_};
    return (original_parent_user_id_ != 0) ? ChildType::kCowClone : ChildType::kNotChild;
  }
  bool is_slice() const { return options_ & kSlice; }
  uint64_t parent_user_id() const override {
    Guard<fbl::Mutex> guard{&lock_};
    return original_parent_user_id_;
  }
  void set_user_id(uint64_t user_id) override {
    VmObject::set_user_id(user_id);
    Guard<fbl::Mutex> guard{&lock_};
    page_attribution_user_id_ = user_id;
  }

  size_t AttributedPagesInRange(uint64_t offset, uint64_t len) const override;

  zx_status_t CommitRange(uint64_t offset, uint64_t len) override;
  zx_status_t DecommitRange(uint64_t offset, uint64_t len) override;
  zx_status_t ZeroRange(uint64_t offset, uint64_t len) override;

  zx_status_t Pin(uint64_t offset, uint64_t len) override;
  void Unpin(uint64_t offset, uint64_t len) override;

  zx_status_t Read(void* ptr, uint64_t offset, size_t len) override;
  zx_status_t Write(const void* ptr, uint64_t offset, size_t len) override;
  zx_status_t Lookup(uint64_t offset, uint64_t len, vmo_lookup_fn_t lookup_fn,
                     void* context) override;

  zx_status_t ReadUser(VmAspace* current_aspace, user_out_ptr<char> ptr, uint64_t offset,
                       size_t len) override;
  zx_status_t WriteUser(VmAspace* current_aspace, user_in_ptr<const char> ptr, uint64_t offset,
                        size_t len) override;

  zx_status_t TakePages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) override;
  zx_status_t SupplyPages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) override;

  void Dump(uint depth, bool verbose) override;

  zx_status_t GetPageLocked(uint64_t offset, uint pf_flags, list_node* free_list,
                            PageRequest* page_request, vm_page_t**, paddr_t*) override
      // Calls a Locked method of the parent, which confuses analysis.
      TA_NO_THREAD_SAFETY_ANALYSIS;

  zx_status_t CreateClone(Resizability resizable, CloneType type, uint64_t offset, uint64_t size,
                          bool copy_name, fbl::RefPtr<VmObject>* child_vmo) override
      // This function reaches into the created child, which confuses analysis.
      TA_NO_THREAD_SAFETY_ANALYSIS;
  // Inserts |hidden_parent| as a hidden parent of |this|. This vmo and |hidden_parent|
  // must have the same lock.
  void InsertHiddenParentLocked(fbl::RefPtr<VmObjectPaged>&& hidden_parent)
      // This accesses both |this| and |hidden_parent|, which confuses analysis.
      TA_NO_THREAD_SAFETY_ANALYSIS;

  void RangeChangeUpdateFromParentLocked(uint64_t offset, uint64_t len, RangeChangeList* list)
      // Called under the parent's lock, which confuses analysis.
      TA_NO_THREAD_SAFETY_ANALYSIS;

  uint32_t GetMappingCachePolicy() const override;
  zx_status_t SetMappingCachePolicy(const uint32_t cache_policy) override;

  void RemoveChild(VmObject* child, Guard<Mutex>&& guard) override
      // Analysis doesn't know that the guard passed to this function is the vmo's lock.
      TA_NO_THREAD_SAFETY_ANALYSIS;
  bool OnChildAddedLocked() override TA_REQ(lock_);

  void DetachSource() override {
    DEBUG_ASSERT(page_source_);
    page_source_->Detach();
  }

  zx_status_t CreateChildSlice(uint64_t offset, uint64_t size, bool copy_name,
                               fbl::RefPtr<VmObject>* child_vmo) override
      // This function reaches into the created child, which confuses analysis.
      TA_NO_THREAD_SAFETY_ANALYSIS;

  uint32_t ScanForZeroPages(bool reclaim) override;

 private:
  // private constructor (use Create())
  VmObjectPaged(uint32_t options, uint32_t pmm_alloc_flags, uint64_t size,
                fbl::RefPtr<vm_lock_t> root_lock, fbl::RefPtr<PageSource> page_source);

  // Initializes the original parent state of the vmo. |offset| is the offset of
  // this vmo in |parent|.
  //
  // This function should be called at most once, even if the parent changes
  // after initialization.
  void InitializeOriginalParentLocked(fbl::RefPtr<VmObject> parent, uint64_t offset)
      // Accesses both parent and child, which confuses analysis.
      TA_NO_THREAD_SAFETY_ANALYSIS;

  static zx_status_t CreateCommon(uint32_t pmm_alloc_flags, uint32_t options, uint64_t size,
                                  fbl::RefPtr<VmObject>* vmo);

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

  zx_status_t PinLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);
  void UnpinLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);

  // Internal decommit range helper that expects the lock to be held. On success it will populate
  // the past in page list with any pages that should be freed.
  zx_status_t DecommitRangeLocked(uint64_t offset, uint64_t len, list_node_t& free_list)
      TA_REQ(lock_);
  zx_status_t ZeroRangeLocked(uint64_t offset, uint64_t len, list_node_t* free_list,
                              Guard<fbl::Mutex>* guard) TA_REQ(lock_);

  fbl::RefPtr<PageSource> GetRootPageSourceLocked() const
      // Walks the parent chain to get the root page source, which confuses analysis.
      TA_NO_THREAD_SAFETY_ANALYSIS;

  bool IsCowClonable() const
      // Walks the parent chain since the root determines clonability.
      TA_NO_THREAD_SAFETY_ANALYSIS;

  // internal check if any pages in a range are pinned
  bool AnyPagesPinnedLocked(uint64_t offset, size_t len) TA_REQ(lock_);

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
                                      Guard<fbl::Mutex>* guard) TA_REQ(lock_);

  // Searches for info for initialization of a page being commited into |this| at |offset|.
  //
  // If an ancestor has a committed page which corresponds to |offset|, returns that page
  // as well as the VmObject and offset which own the page. If no ancestor has a committed
  // page for the offset, returns null as well as the VmObject/offset which need to be queried
  // to populate the page.
  //
  // It is an error to call this when |this| has a committed page at |offset|.
  vm_page_t* FindInitialPageContentLocked(uint64_t offset, uint pf_flags, VmObject** owner_out,
                                          uint64_t* owner_offset_out)
      // Walks the child chain, which confuses analysis.
      TA_NO_THREAD_SAFETY_ANALYSIS;

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
                                vm_page_t* page, uint64_t owner_offset)
      // Walking through the ancestors confuses analysis.
      TA_NO_THREAD_SAFETY_ANALYSIS;

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
  bool IsUniAccessibleLocked(vm_page_t* page, uint64_t offset) const
      // Reaching into the children confuses analysis
      TA_NO_THREAD_SAFETY_ANALYSIS;

  // ::CloneCowPageLocked helper function that ensures contigous vmos remain contiguous.
  //
  // In general, it does not matter which vmo gets which physical page when forking pages in
  // hidden vmos. However, if there are COW clones of a contiguous vmo, the original vmo
  // must always see the original physical pages, so that it always looks contiguous to
  // userspace.  This function is responsible for fixing up any violations to this property
  // introduced by the primary page forking logic in ::CloneCowPageLocked.
  //
  // |page_owner| is the original owner of the page being forked, and |page_owner_offset|
  // is the offset of the original page. |last_contig| is the last contiguous vmo between
  // |this| and |page_owner| that can also no longer see the desired page.
  void ContiguousCowFixupLocked(VmObjectPaged* page_owner, uint64_t page_owner_offset,
                                VmObjectPaged* last_contig, uint64_t last_contig_offset)
      // Walking through the ancestors confuses analysis.
      TA_NO_THREAD_SAFETY_ANALYSIS;

  // Releases this vmo's reference to any ancestor vmo's COW pages, for the range [start, end)
  // in this vmo. This is done by either setting the pages' split bits (if something else
  // can access the pages) or by freeing the pages onto |free_list| (if nothing else can
  // access the pages).
  //
  // This function recursively invokes itself for regions of the parent vmo which are
  // not accessible by the sibling vmo.
  void ReleaseCowParentPagesLocked(uint64_t start, uint64_t end, list_node_t* free_list)
      // Walking the clone tree confuses analysis
      TA_NO_THREAD_SAFETY_ANALYSIS;

  // Helper function for ReleaseCowParentPagesLocked that processes pages which are visible
  // to both children as well as updates parent_(offset_)limit_.
  void ReleaseCowParentPagesLockedHelper(uint64_t start, uint64_t end, list_node_t* free_list)
      // Calling into the parents confuses analysis
      TA_NO_THREAD_SAFETY_ANALYSIS;

  // Updates the parent limits of all children so that they will never be able to
  // see above |new_size| in this vmo, even if the vmo is enlarged in the future.
  void UpdateChildParentLimitsLocked(uint64_t new_size)
      // Calling into the children confuses analysis
      TA_NO_THREAD_SAFETY_ANALYSIS;

  // When cleaning up a hidden vmo, merges the hidden vmo's content (e.g. page list, view
  // of the parent) into the remaining child.
  void MergeContentWithChildLocked(VmObjectPaged* removed, bool removed_left)
      // Accesses into the child confuse analysis
      TA_NO_THREAD_SAFETY_ANALYSIS;

  // Only valid to be called when is_slice() is true and returns the first parent of this
  // hierarchy that is not a slice. The offset of this slice within that VmObjectPaged is set as
  // the output.
  VmObjectPaged* PagedParentOfSliceLocked(uint64_t* offset)
      // Calling into the parent confuses analysis
      TA_NO_THREAD_SAFETY_ANALYSIS;

  // Zeroes a partial range in a page. May use CallUnlocked on the passed in guard. The page to zero
  // is looked up using page_base_offset, and will be committed if needed. The range of
  // [zero_start_offset, zero_end_offset) is relative to the page and so [0, PAGE_SIZE) would zero
  // the entire page.
  zx_status_t ZeroPartialPage(uint64_t page_base_offset, uint64_t zero_start_offset,
                              uint64_t zero_end_offset, Guard<fbl::Mutex>* guard) TA_REQ(lock_);

  // Outside of initialization/destruction, hidden vmos always have two children. For
  // clarity, whichever child is first in the list is the 'left' child, and whichever
  // child is second is the 'right' child. Children of a paged vmo will always be paged
  // vmos themselves.
  VmObjectPaged& left_child_locked() TA_REQ(lock_) {
    DEBUG_ASSERT(is_hidden());
    DEBUG_ASSERT(children_list_len_ == 2);
    DEBUG_ASSERT(children_list_.front().is_paged());
    return static_cast<VmObjectPaged&>(children_list_.front());
  }
  VmObjectPaged& right_child_locked() TA_REQ(lock_) {
    DEBUG_ASSERT(is_hidden());
    DEBUG_ASSERT(children_list_len_ == 2);
    DEBUG_ASSERT(children_list_.back().is_paged());
    return static_cast<VmObjectPaged&>(children_list_.back());
  }
  const VmObjectPaged& left_child_locked() const TA_REQ(lock_) {
    DEBUG_ASSERT(is_hidden());
    DEBUG_ASSERT(children_list_len_ == 2);
    DEBUG_ASSERT(children_list_.front().is_paged());
    return static_cast<const VmObjectPaged&>(children_list_.front());
  }
  const VmObjectPaged& right_child_locked() const TA_REQ(lock_) {
    DEBUG_ASSERT(is_hidden());
    DEBUG_ASSERT(children_list_len_ == 2);
    DEBUG_ASSERT(children_list_.back().is_paged());
    return static_cast<const VmObjectPaged&>(children_list_.back());
  }

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
  // parent_limit_, this value does not directly impact page lookup.
  uint64_t parent_start_limit_ TA_GUARDED(lock_) = 0;
  const uint32_t pmm_alloc_flags_ = PMM_ALLOC_FLAG_ANY;
  uint32_t cache_policy_ TA_GUARDED(lock_) = ARCH_MMU_FLAG_CACHED;

  // Flag which is true if there was a call to ::ReleaseCowParentPagesLocked which was
  // not able to update the parent limits. When this is not set, it is sometimes
  // possible for ::MergeContentWithChildLocked to do significantly less work.
  bool partial_cow_release_ TA_GUARDED(lock_) = false;

  // parent pointer (may be null)
  fbl::RefPtr<VmObject> parent_ TA_GUARDED(lock_);
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

  // Counts the total number of pages pinned by ::Pin. If one page is pinned n times, it
  // contributes n to this count. However, this does not include pages pinned when creating
  // a contiguous vmo.
  uint64_t pinned_page_count_ TA_GUARDED(lock_) = 0;

  // The page source, if any.
  const fbl::RefPtr<PageSource> page_source_;

  // a tree of pages
  VmPageList page_list_ TA_GUARDED(lock_);
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_VM_OBJECT_PAGED_H_
