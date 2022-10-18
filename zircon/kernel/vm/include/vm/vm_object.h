// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_VM_OBJECT_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_VM_OBJECT_H_

#include <align.h>
#include <lib/fit/function.h>
#include <lib/user_copy/user_iovec.h>
#include <lib/user_copy/user_ptr.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stdint.h>
#include <zircon/listnode.h>
#include <zircon/syscalls-next.h>
#include <zircon/types.h>

#include <arch/aspace.h>
#include <fbl/array.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fbl/name.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_counted_upgradeable.h>
#include <fbl/ref_ptr.h>
#include <kernel/lockdep.h>
#include <kernel/mutex.h>
#include <ktl/move.h>
#include <vm/page.h>
#include <vm/vm.h>
#include <vm/vm_page_list.h>

class VmMapping;
class LazyPageRequest;
class VmObjectPaged;
class VmAspace;
class VmObject;
class VmHierarchyState;

class VmObjectChildObserver {
 public:
  virtual void OnZeroChild() = 0;
  virtual void OnOneChild() = 0;
};

// Typesafe enum for resizability arguments.
enum class Resizability {
  Resizable,
  NonResizable,
};

// Argument which specifies the type of clone.
enum class CloneType {
  Snapshot,
  PrivatePagerCopy,
};

namespace internal {
struct ChildListTag {};
struct GlobalListTag {};
}  // namespace internal

// Base class for any objects that want to be part of the VMO hierarchy and share some state,
// including a lock. Additionally all objects in the hierarchy can become part of the same
// deferred deletion mechanism to avoid unbounded chained destructors.
class VmHierarchyBase : public fbl::RefCountedUpgradeable<VmHierarchyBase>,
                        public fbl::Recyclable<VmHierarchyBase> {
 public:
  explicit VmHierarchyBase(fbl::RefPtr<VmHierarchyState> state);

  Lock<CriticalMutex>* lock() const TA_RET_CAP(lock_) { return &lock_; }
  Lock<CriticalMutex>& lock_ref() const TA_RET_CAP(lock_) { return lock_; }

 protected:
  // private destructor, only called from refptr
  virtual ~VmHierarchyBase() = default;
  friend fbl::RefPtr<VmHierarchyBase>;
  // Similar to the destructor, fbl_recycle() needs to be virtual so DoDeferredDelete() can take a
  // RefPtr<VmHierarchyBase> but when fbl_recycle() is called the VmCowPages::fbl_recycle() will be
  // run.
  virtual void fbl_recycle() { delete this; }
  friend class fbl::Recyclable<VmHierarchyBase>;

  // The lock which protects this class. All objects in a clone tree
  // share the same lock.
  Lock<CriticalMutex>& lock_;
  // Pointer to state shared across all objects in a hierarchy.
  fbl::RefPtr<VmHierarchyState> const hierarchy_state_ptr_;

  // Convenience helpers that forward operations to the referenced hierarchy state.
  void IncrementHierarchyGenerationCountLocked() TA_REQ(lock_);
  uint64_t GetHierarchyGenerationCountLocked() const TA_REQ(lock_);

 private:
  using DeferredDeleteState = fbl::SinglyLinkedListNodeState<fbl::RefPtr<VmHierarchyBase>>;
  struct DeferredDeleteTraits {
    static DeferredDeleteState& node_state(VmHierarchyBase& vm) {
      return vm.deferred_delete_state_;
    }
  };
  friend struct DeferredDeleteTraits;
  friend VmHierarchyState;
  DeferredDeleteState deferred_delete_state_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(VmHierarchyBase);
};

class VmHierarchyState : public fbl::RefCounted<VmHierarchyState> {
 public:
  VmHierarchyState() = default;
  ~VmHierarchyState() = default;

  Lock<CriticalMutex>* lock() TA_RET_CAP(lock_) { return &lock_; }
  Lock<CriticalMutex>& lock_ref() TA_RET_CAP(lock_) { return lock_; }

  // Drops the refptr to the given object by either placing it on the deferred delete list for
  // another thread already running deferred delete to drop, or drops itself.
  // This can be used to avoid unbounded recursion when dropping chained refptrs, as found in
  // vmo parent_ refs.
  void DoDeferredDelete(fbl::RefPtr<VmHierarchyBase> vmo) TA_EXCL(lock_);

  // This should be called whenever a change is made to the VMO tree or the VMO's page list, that
  // could result in page attribution counts to change for any VMO in this tree.
  void IncrementHierarchyGenerationCountLocked() TA_REQ(lock_) {
    DEBUG_ASSERT(hierarchy_generation_count_ != 0);
    hierarchy_generation_count_++;
  }

  // Get the current generation count.
  uint64_t GetHierarchyGenerationCountLocked() const TA_REQ(lock_) {
    DEBUG_ASSERT(hierarchy_generation_count_ != 0);
    return hierarchy_generation_count_;
  }

 private:
  DECLARE_CRITICAL_MUTEX(VmHierarchyState) lock_;
  bool running_delete_ TA_GUARDED(lock_) = false;
  fbl::SinglyLinkedListCustomTraits<fbl::RefPtr<VmHierarchyBase>,
                                    VmHierarchyBase::DeferredDeleteTraits>
      delete_list_ TA_GUARDED(lock_);

  // Each VMO hierarchy has a generation count, which is incremented on any change to the hierarchy
  // - either in the VMO tree, or the page lists of VMO's.
  //
  // The generation count is used to implement caching for page attribution counts, which get
  // queried frequently to periodically track memory usage on the system. Attributing pages to a
  // VMO is an expensive operation and involves walking the VMO tree, quite often multiple times.
  // If the generation count does not change between two successive queries, we can avoid
  // re-counting attributed pages, and simply return the previously cached value.
  //
  // The generation count starts at 1 to ensure that there can be no cached values initially; the
  // cached generation count starts at 0.
  uint64_t hierarchy_generation_count_ TA_GUARDED(lock_) = 1;
};

inline void VmHierarchyBase::IncrementHierarchyGenerationCountLocked() {
  AssertHeld(hierarchy_state_ptr_->lock_ref());
  hierarchy_state_ptr_->IncrementHierarchyGenerationCountLocked();
}

inline uint64_t VmHierarchyBase::GetHierarchyGenerationCountLocked() const {
  AssertHeld(hierarchy_state_ptr_->lock_ref());
  return hierarchy_state_ptr_->GetHierarchyGenerationCountLocked();
}

// Cursor to allow for walking global vmo lists without needing to hold the lock protecting them all
// the time. This can be required to enforce order of acquisition with another lock (as in the case
// of |discardable_reclaim_candidates_|), or it can be desirable for performance reasons (as in the
// case of |all_vmos_|).
// In practice at most one cursor is expected to exist, but as the cursor list is global the
// overhead of being generic to support multiple cursors is negligible.
//
// |ObjType| is the type of object being tracked in the list (VmObject, VmCowPages etc).
// |LockType| is the singleton global lock used to protect the list.
// |ListType| is the type of the global vmo list.
// |ListIteratorType| is the iterator for |ListType|.
template <typename ObjType, typename LockType, typename ListType, typename ListIteratorType>
class VmoCursor
    : public fbl::DoublyLinkedListable<VmoCursor<ObjType, LockType, ListType, ListIteratorType>*> {
 public:
  VmoCursor() = delete;

  using CursorsList =
      fbl::DoublyLinkedList<VmoCursor<ObjType, LockType, ListType, ListIteratorType>*>;

  // Constructor takes as arguments the global lock, the global vmo list, and the global list of
  // cursors to add the newly created cursor to. Should be called while holding the global |lock|.
  VmoCursor(LockType* lock, ListType& vmos, CursorsList& cursors)
      : lock_(*lock), vmos_list_(vmos), cursors_list_(cursors) {
    AssertHeld(lock_);

    if (!vmos_list_.is_empty()) {
      vmos_iter_ = vmos_list_.begin();
    } else {
      vmos_iter_ = vmos_list_.end();
    }

    cursors_list_.push_front(this);
  }

  // Destructor removes this cursor from the global list of all cursors.
  ~VmoCursor() TA_REQ(lock_) { cursors_list_.erase(*this); }

  // Advance the cursor and return the next element or nullptr if at the end of the list.
  //
  // Once |Next| has returned nullptr, all subsequent calls will return nullptr.
  //
  // The caller must hold the global |lock_|.
  ObjType* Next() TA_REQ(lock_) {
    if (vmos_iter_ == vmos_list_.end()) {
      return nullptr;
    }

    ObjType* result = &*vmos_iter_;
    vmos_iter_++;
    return result;
  }

  // If the next element is |h|, advance the cursor past it.
  //
  // The caller must hold the global |lock_|.
  void AdvanceIf(const ObjType* h) TA_REQ(lock_) {
    if (vmos_iter_ != vmos_list_.end()) {
      if (&*vmos_iter_ == h) {
        vmos_iter_++;
      }
    }
  }

  // Advances all the cursors in |cursors_list|, calling |AdvanceIf(h)| on each cursor.
  //
  // The caller must hold the global lock protecting the |cursors_list|.
  static void AdvanceCursors(CursorsList& cursors_list, const ObjType* h) {
    for (auto& cursor : cursors_list) {
      AssertHeld(cursor.lock_ref());
      cursor.AdvanceIf(h);
    }
  }

  LockType& lock_ref() TA_RET_CAP(lock_) { return lock_; }

 private:
  VmoCursor(const VmoCursor&) = delete;
  VmoCursor& operator=(const VmoCursor&) = delete;
  VmoCursor(VmoCursor&&) = delete;
  VmoCursor& operator=(VmoCursor&&) = delete;

  LockType& lock_;
  ListType& vmos_list_ TA_GUARDED(lock_);
  CursorsList& cursors_list_ TA_GUARDED(lock_);

  ListIteratorType vmos_iter_ TA_GUARDED(lock_);
};

// The base vm object that holds a range of bytes of data
//
// Can be created without mapping and used as a container of data, or mappable
// into an address space via VmAddressRegion::CreateVmMapping
class VmObject : public VmHierarchyBase,
                 public fbl::ContainableBaseClasses<
                     fbl::TaggedDoublyLinkedListable<VmObject*, internal::ChildListTag>,
                     fbl::TaggedDoublyLinkedListable<VmObject*, internal::GlobalListTag>> {
 public:
  // public API
  virtual zx_status_t Resize(uint64_t size) { return ZX_ERR_NOT_SUPPORTED; }

  virtual uint64_t size() const TA_EXCL(lock_) { return 0; }
  virtual uint32_t create_options() const { return 0; }

  // Returns true if the object is backed by RAM.
  virtual bool is_paged() const { return false; }
  // Returns true if the object is backed by a contiguous range of physical
  // memory.
  virtual bool is_contiguous() const { return false; }
  // Returns true if the object size can be changed.
  virtual bool is_resizable() const { return false; }
  // Returns true if the object's pages are discardable by the kernel.
  virtual bool is_discardable() const { return false; }
  // Returns true if the VMO was created via CreatePagerVmo().
  virtual bool is_user_pager_backed() const { return false; }
  // Returns true if the VMO supports CloneType::PrivatePagerCopy.
  virtual bool is_private_pager_copy_supported() const { return false; }
  // Returns true if the VMO's pages require dirty bit tracking.
  virtual bool is_dirty_tracked_locked() const TA_REQ(lock_) { return false; }
  // Marks the VMO as modified if the VMO tracks modified state (only supported for pager-backed
  // VMOs).
  virtual void mark_modified_locked() TA_REQ(lock_) {}

  struct AttributionCounts {
    size_t uncompressed = 0;
    size_t compressed = 0;

    const AttributionCounts& operator+=(const AttributionCounts& other) {
      uncompressed += other.uncompressed;
      compressed += other.compressed;
      return *this;
    }

    bool operator==(const AttributionCounts& other) const {
      return uncompressed == other.uncompressed && compressed == other.compressed;
    }

    bool operator!=(const AttributionCounts& other) const { return !(*this == other); }
  };

  // Returns the number of physical pages currently attributed to the
  // object where (offset <= page_offset < offset+len).
  // |offset| and |len| are in bytes.
  virtual AttributionCounts AttributedPagesInRange(uint64_t offset, uint64_t len) const {
    return AttributionCounts{};
  }
  // Returns the number of physical pages currently attributed to the object.
  AttributionCounts AttributedPages() const { return AttributedPagesInRange(0, size()); }

  // find physical pages to back the range of the object
  // May block on user pager requests and must be called without locks held.
  virtual zx_status_t CommitRange(uint64_t offset, uint64_t len) { return ZX_ERR_NOT_SUPPORTED; }

  // find physical pages to back the range of the object and pin them.
  // |len| must be non-zero. |write| indicates whether the range is being pinned for a write or a
  // read.
  // May block on user pager requests and must be called without locks held.
  virtual zx_status_t CommitRangePinned(uint64_t offset, uint64_t len, bool write) = 0;

  // free a range of the vmo back to the default state
  virtual zx_status_t DecommitRange(uint64_t offset, uint64_t len) { return ZX_ERR_NOT_SUPPORTED; }

  // Zero a range of the VMO. May release physical pages in the process.
  // May block on user pager requests and must be called without locks held.
  virtual zx_status_t ZeroRange(uint64_t offset, uint64_t len) { return ZX_ERR_NOT_SUPPORTED; }

  // Unpin the given range of the vmo.  This asserts if it tries to unpin a
  // page that is already not pinned (do not expose this function to
  // usermode).
  virtual void Unpin(uint64_t offset, uint64_t len) = 0;

  // Checks if all pages in the provided range are pinned.
  // This is only intended to be used for debugging checks.
  virtual bool DebugIsRangePinned(uint64_t offset, uint64_t len) = 0;

  // Lock a range from being discarded by the kernel. Can fail if the range was already discarded.
  virtual zx_status_t TryLockRange(uint64_t offset, uint64_t len) { return ZX_ERR_NOT_SUPPORTED; }

  // Lock a range from being discarded by the kernel. Guaranteed to succeed. |lock_state_out| is
  // populated with relevant information about the locked and discarded ranges.
  virtual zx_status_t LockRange(uint64_t offset, uint64_t len,
                                zx_vmo_lock_state_t* lock_state_out) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Unlock a range, making it available for the kernel to discard. The range could have been locked
  // either by |TryLockRange| or |LockRange|.
  virtual zx_status_t UnlockRange(uint64_t offset, uint64_t len) { return ZX_ERR_NOT_SUPPORTED; }

  // read/write operators against kernel pointers only
  // May block on user pager requests and must be called without locks held.

  virtual zx_status_t Read(void* ptr, uint64_t offset, size_t len) { return ZX_ERR_NOT_SUPPORTED; }
  virtual zx_status_t Write(const void* ptr, uint64_t offset, size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // execute lookup_fn on a given range of physical addresses within the vmo. Only pages that are
  // present and writable in this VMO will be enumerated. Any copy-on-write pages in our parent
  // will not be enumerated. The physical addresses given to the lookup_fn should not be retained in
  // any way unless the range has also been pinned by the caller. Offsets provided will be in
  // relation to the object being queried, even if pages are actually from a parent object where
  // this is a slice.
  // Ranges of length zero are considered invalid and will return ZX_ERR_INVALID_ARGS. The lookup_fn
  // can terminate iteration early by returning ZX_ERR_STOP.
  using LookupFunction =
      fit::inline_function<zx_status_t(uint64_t offset, paddr_t pa), 4 * sizeof(void*)>;
  virtual zx_status_t Lookup(uint64_t offset, uint64_t len, LookupFunction lookup_fn) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Attempts to lookup the given range in the VMO. If it exists and is physically contiguous
  // returns the paddr of the start of the range. The offset must be page aligned.
  // Ranges of length zero are considered invalid and will return ZX_ERR_INVALID_ARGS.
  // A null |paddr| may be passed to just check for contiguity.
  virtual zx_status_t LookupContiguous(uint64_t offset, uint64_t len, paddr_t* out_paddr) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // read/write operators against user space pointers only
  //
  // The |out_actual| field will be set to the number of bytes successfully processed, even upon
  // error. This allows for callers to still pass on this bytes transferred if a particular
  // error was expected.
  //
  // May block on user pager requests and must be called without locks held.
  //
  // Bytes are guaranteed to be transferred in order from low to high offset.
  virtual zx_status_t ReadUser(VmAspace* current_aspace, user_out_ptr<char> ptr, uint64_t offset,
                               size_t len, size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  virtual zx_status_t ReadUserVector(VmAspace* current_aspace, user_out_iovec_t vec,
                                     uint64_t offset, size_t len, size_t* out_actual);

  // |OnWriteBytesTransferredCallback| is guaranteed to be called after bytes have been successfully
  // transferred from the user source to the VMO and will be called before the VMO lock is dropped.
  // As a result, operations performed within the callback should not take any other locks or be
  // long-running.
  using OnWriteBytesTransferredCallback = fit::inline_function<void(uint64_t offset, size_t len)>;
  virtual zx_status_t WriteUser(VmAspace* current_aspace, user_in_ptr<const char> ptr,
                                uint64_t offset, size_t len, size_t* out_actual,
                                const OnWriteBytesTransferredCallback& on_bytes_transferred) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  virtual zx_status_t WriteUserVector(VmAspace* current_aspace, user_in_iovec_t vec,
                                      uint64_t offset, size_t len, size_t* out_actual,
                                      const OnWriteBytesTransferredCallback& on_bytes_transferred);

  // Removes the pages from this vmo in the range [offset, offset + len) and returns
  // them in pages.  This vmo must be a paged vmo with no parent, and it cannot have any
  // pinned pages in the source range. |offset| and |len| must be page aligned.
  virtual zx_status_t TakePages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Supplies this vmo with pages for the range [offset, offset + len). If this vmo
  // already has pages in the target range, the corresponding pages in |pages| will be
  // freed, instead of being moved into this vmo. |offset| and |len| must be page aligned.
  virtual zx_status_t SupplyPages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Indicates that page requests in the range [offset, offset + len) could not be fulfilled.
  // |error_status| specifies the error encountered. |offset| and |len| must be page aligned.
  virtual zx_status_t FailPageRequests(uint64_t offset, uint64_t len, zx_status_t error_status) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Dirties pages in the vmo in the range [offset, offset + len).
  virtual zx_status_t DirtyPages(uint64_t offset, uint64_t len) { return ZX_ERR_NOT_SUPPORTED; }

  using DirtyRangeEnumerateFunction = fit::inline_function<zx_status_t(
      uint64_t range_offset, uint64_t range_len, bool range_is_zero)>;
  // Enumerates dirty ranges in the range [offset, offset + len) in ascending order, updating any
  // relevant VMO internal state required to perform the enumeration, and calls |dirty_range_fn| on
  // each dirty range (spanning [range_offset, range_offset + range_len) where |range_is_zero|
  // indicates whether the range is all zeros). |dirty_range_fn| can return ZX_ERR_NEXT to continue
  // with the enumeration, ZX_ERR_STOP to terminate the enumeration successfully, and any other
  // error code to terminate the enumeration early with that error code.
  virtual zx_status_t EnumerateDirtyRanges(uint64_t offset, uint64_t len,
                                           DirtyRangeEnumerateFunction&& dirty_range_fn) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Query pager relevant VMO stats, e.g. whether the VMO has been modified. If |reset| is set to
  // true, the queried stats are reset as well, potentially affecting the queried state returned by
  // future calls to this function.
  virtual zx_status_t QueryPagerVmoStats(bool reset, zx_pager_vmo_stats_t* stats) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Indicates start of writeback for the range [offset, offset + len). Any Dirty pages in the range
  // are transitioned to AwaitingClean, in preparation for transition to Clean when the writeback is
  // done (See VmCowPages::DirtyState for details of these states). |offset| and |len| must be page
  // aligned. |is_zero_range| specifies whether the caller intends to write back the specified range
  // as zeros.
  virtual zx_status_t WritebackBegin(uint64_t offset, uint64_t len, bool is_zero_range) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Indicates end of writeback for the range [offset, offset + len). Any AwaitingClean pages in the
  // range are transitioned to Clean (See VmCowPages::DirtyState for details of these states).
  // |offset| and |len| must be page aligned.
  virtual zx_status_t WritebackEnd(uint64_t offset, uint64_t len) { return ZX_ERR_NOT_SUPPORTED; }

  enum EvictionHint {
    DontNeed,
    AlwaysNeed,
  };
  // Hint how the specified range is intended to be used, so that the hint can be taken into
  // consideration when reclaiming pages under memory pressure (if applicable).
  // May block on user pager requests and must be called without locks held.
  virtual zx_status_t HintRange(uint64_t offset, uint64_t len, EvictionHint hint) {
    // Hinting trivially succeeds for unsupported VMO types.
    return ZX_OK;
  }

  // TODO(fxb/101641): This is a temporary solution and needs to be replaced with something that is
  // formalized.
  virtual void MarkAsLatencySensitive() {
    // This does nothing by default.
  }

  // The associated VmObjectDispatcher will set an observer to notify user mode.
  void SetChildObserver(VmObjectChildObserver* child_observer);

  // Returns a null-terminated name, or the empty string if set_name() has not
  // been called.
  void get_name(char* out_name, size_t len) const;

  // Sets the name of the object. May truncate internally. |len| is the size
  // of the buffer pointed to by |name|.
  zx_status_t set_name(const char* name, size_t len);

  // Returns a user ID associated with this VMO, or zero.
  // Typically used to hold a zircon koid for Dispatcher-wrapped VMOs.
  uint64_t user_id() const;
  uint64_t user_id_locked() const TA_REQ(lock_);

  // Returns the parent's user_id() if this VMO has a parent,
  // otherwise returns zero.
  virtual uint64_t parent_user_id() const = 0;

  // Sets the value returned by |user_id()|. May only be called once.
  //
  // Derived types overriding this method are expected to call it from their override.
  virtual void set_user_id(uint64_t user_id);

  virtual void Dump(uint depth, bool verbose) = 0;

  // perform a cache maintenance operation against the vmo.
  enum class CacheOpType { Invalidate, Clean, CleanInvalidate, Sync };
  virtual zx_status_t CacheOp(uint64_t offset, uint64_t len, CacheOpType type) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  virtual uint32_t GetMappingCachePolicy() const = 0;
  virtual zx_status_t SetMappingCachePolicy(const uint32_t cache_policy) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // create a copy-on-write clone vmo at the page-aligned offset and length
  // note: it's okay to start or extend past the size of the parent
  virtual zx_status_t CreateClone(Resizability resizable, CloneType type, uint64_t offset,
                                  uint64_t size, bool copy_name, fbl::RefPtr<VmObject>* child_vmo) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  virtual zx_status_t CreateChildSlice(uint64_t offset, uint64_t size, bool copy_name,
                                       fbl::RefPtr<VmObject>* child_vmo) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  enum ChildType { kNotChild, kCowClone, kSlice };
  virtual ChildType child_type() const = 0;

  virtual uint64_t HeapAllocationBytes() const { return 0; }

  // Number of times pages have been evicted over the lifetime of this VMO. Evicted counts for any
  // decommit style event such as user pager eviction or zero page merging. One eviction event
  // could count for multiple pages being evicted, if those pages were evicted as a group.
  virtual uint64_t EvictionEventCount() const { return 0; }

  // Get a pointer to the page structure and/or physical address at the specified offset.
  // valid flags are VMM_PF_FLAG_*.
  //
  // |page_request| must be non-null if any flags in VMM_PF_FLAG_FAULT_MASK are set, unless
  // the caller knows that the vm object is not paged.
  //
  // Returns ZX_ERR_SHOULD_WAIT if the caller should try again after waiting on the
  // PageRequest.
  //
  // Returns ZX_ERR_NEXT if |page_request| supports batching and the current request
  // can be batched. The caller should continue to make successive GetPage requests
  // until this returns ZX_ERR_SHOULD_WAIT. If the caller runs out of requests, it
  // should finalize the request with PageSource::FinalizeRequest.
  zx_status_t GetPage(uint64_t offset, uint pf_flags, list_node* alloc_list,
                      LazyPageRequest* page_request, vm_page_t** page, paddr_t* pa) {
    Guard<CriticalMutex> guard{&lock_};
    return GetPageLocked(offset, pf_flags, alloc_list, page_request, page, pa);
  }

  // Helper variant of GetPage that will retry the operation after waiting on a PageRequest if
  // required.
  // Must not be called with any locks held.
  zx_status_t GetPageBlocking(uint64_t offset, uint pf_flags, list_node* alloc_list,
                              vm_page_t** page, paddr_t* pa);

  // See VmObject::GetPage
  zx_status_t GetPageLocked(uint64_t offset, uint pf_flags, list_node* alloc_list,
                            LazyPageRequest* page_request, vm_page_t** page, paddr_t* pa)
      TA_REQ(lock_) {
    __UNINITIALIZED LookupInfo lookup;
    zx_status_t status = LookupPagesLocked(offset, pf_flags, DirtyTrackingAction::None, 1,
                                           alloc_list, page_request, &lookup);
    if (status == ZX_OK) {
      DEBUG_ASSERT(lookup.num_pages == 1);
      if (unlikely(page)) {
        // This reverse lookup isn't very expensive, and page_out is very rarely requested anyway.
        *page = paddr_to_vm_page(lookup.paddrs[0]);
      }
      if (pa) {
        *pa = lookup.paddrs[0];
      }
    }
    return status;
  }

  // The dirty tracking action to be applied by LookupPagesLocked to the pages it returns.
  enum class DirtyTrackingAction : uint8_t {
    // The caller does not intend to modify any page contents.
    None = 0,
    // The caller intends to modify the contents of all looked up pages on a write (i.e. if
    // the VMM_PF_FLAG_WRITE flag is set).
    DirtyAllPagesOnWrite,
    // TODO(rashaeqbal): Consider adding an option that dirties only the first page for batch
    // mapping faults.
  };
  // Output struct for LookupPagesLocked to return a run of pages.
  struct LookupInfo {
    // Helper to add a paddr to the next slot in the array.
    void add_page(paddr_t paddr) {
      ASSERT(num_pages < kMaxPages);
      paddrs[num_pages] = paddr;
      num_pages++;
    }
    // This value is chosen conservatively as this structure is allocated directly on the stack, and
    // larger values have diminishing returns for the benefit they provide.
    static constexpr uint64_t kMaxPages = 16;
    paddr_t paddrs[kMaxPages];
    uint64_t num_pages = 0;
    // If true the pages returned may be written to, even if the write flag was not specified in
    // the lookup.
    bool writable;
  };
  // See GetPage for a description of the core functionality.
  // Beyond GetPage this allows for retrieving information about multiple pages, storing them in a
  // |LookupInfo| output struct. The |max_out_pages| is required to be strictly greater than 0, but
  // not greater than LookupInfo::kMaxPages. |mark_dirty| specifies whether pages looked up for a
  // write (VMM_PF_FLAG_WRITE) should be marked dirty, e.g. when called from a zx_vmo_write or a
  // write fault. Note that |mark_dirty| only carries any meaning if VMM_PF_FLAG_WRITE is set, as it
  // is specifying the dirty action on a write.
  //
  // Collecting additional pages essentially treats the VMO's content as immutable, and will not
  // perform page write forking or any page allocations, but may update page tracking metadata. For
  // example, if a lookup is requested for a multi-page range with the write flag set, looked up
  // pages in the range may be marked dirty in preparation for the write.
  //
  // However returning additional pages is strictly optional and the caller may not infer anything
  // based on absence of these pages. That is, for any additional pages that are returned, it is
  // guaranteed that GetPage would have returned exactly the same page. The additional lookups
  // treating the VMO immutable makes this suitable for performing optimistic lookups without
  // impacting memory usage.
  virtual zx_status_t LookupPagesLocked(uint64_t offset, uint pf_flags,
                                        DirtyTrackingAction mark_dirty, uint64_t max_out_pages,
                                        list_node* alloc_list, LazyPageRequest* page_request,
                                        LookupInfo* out) TA_REQ(lock_) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  void AddMappingLocked(VmMapping* r) TA_REQ(lock_);
  void RemoveMappingLocked(VmMapping* r) TA_REQ(lock_);
  uint32_t num_mappings() const;

  // Returns true if this VMO is mapped into any VmAspace whose is_user()
  // returns true.
  bool IsMappedByUser() const;

  // Returns an estimate of the number of unique VmAspaces that this object
  // is mapped into.
  uint32_t share_count() const;

  // Adds a child to this vmo and returns true if the dispatcher which matches
  // user_id should be notified about the first child being added.
  bool AddChildLocked(VmObject* child) TA_REQ(lock_);

  // Notifies the child observer that there is one child.
  void NotifyOneChild() TA_EXCL(lock_);

  // Removes the child |child| from this vmo.
  //
  // Subclasses which override this function should be sure that ::DropChildLocked
  // and ::OnUserChildRemoved are called where appropriate.
  //
  // |guard| must be this vmo's lock.
  virtual void RemoveChild(VmObject* child, Guard<CriticalMutex>&& guard) TA_REQ(lock_);

  // Drops |c| from the child list without going through the full removal
  // process. ::RemoveChild is probably what you want here.
  void DropChildLocked(VmObject* c) TA_REQ(lock_);
  void ReplaceChildLocked(VmObject* old, VmObject* new_child) TA_REQ(lock_);
  uint32_t num_user_children() const;
  uint32_t num_children() const;

  // Function that should be invoked when a userspace visible child of
  // this vmo is removed. Updates state and notifies userspace if necessary.
  //
  // The guard passed to this function is the vmo's lock.
  void OnUserChildRemoved(Guard<CriticalMutex>&& guard) TA_REQ(lock_);

  // Called by AddChildLocked. VmObject::OnChildAddedLocked eventually needs to be invoked
  // on the VmObject which is held by the dispatcher which matches |user_id|. Implementations
  // should forward this call towards that VmObject and eventually call this class's
  // implementation.
  virtual bool OnChildAddedLocked() TA_REQ(lock_);

  // Calls the provided |func(const VmObject&)| on every VMO in the system,
  // from oldest to newest. Stops if |func| returns an error, returning the
  // error value.
  template <typename T>
  static zx_status_t ForEach(T func) {
    Guard<CriticalMutex> guard{AllVmosLock::Get()};
    for (const auto& iter : all_vmos_) {
      zx_status_t s = func(iter);
      if (s != ZX_OK) {
        return s;
      }
    }
    return ZX_OK;
  }

  // Detaches the underlying page source, if present. Can be called multiple times.
  virtual void DetachSource() {}

 protected:
  explicit VmObject(fbl::RefPtr<VmHierarchyState> hierarchy_state_ptr);

  // private destructor, only called from refptr
  virtual ~VmObject();
  friend fbl::RefPtr<VmObject>;

  DISALLOW_COPY_ASSIGN_AND_MOVE(VmObject);

  void AddToGlobalList();
  void RemoveFromGlobalList();
  bool InGlobalList() const { return fbl::InContainer<internal::GlobalListTag>(*this); }

  // Performs the requested cache op against a physical address range. The requested physical range
  // must be accessible via the physmap.
  static void CacheOpPhys(paddr_t pa, uint64_t length, CacheOpType op,
                          ArchVmICacheConsistencyManager& cm);

  // magic value
  fbl::Canary<fbl::magic("VMO_")> canary_;

  // list of every mapping
  fbl::DoublyLinkedList<VmMapping*> mapping_list_ TA_GUARDED(lock_);

  // list of every child
  fbl::TaggedDoublyLinkedList<VmObject*, internal::ChildListTag> children_list_ TA_GUARDED(lock_);

  // lengths of corresponding lists
  uint32_t mapping_list_len_ TA_GUARDED(lock_) = 0;
  uint32_t children_list_len_ TA_GUARDED(lock_) = 0;

  uint64_t user_id_ TA_GUARDED(lock_) = 0;
  // The count of the number of children of this vmo as understood by userspace. This
  // field only makes sense in VmObjects directly owned by dispatchers. In particular,
  // it is not meaningful for hidden VmObjectPaged.
  uint32_t user_child_count_ TA_GUARDED(lock_) = 0;

  // The user-friendly VMO name. For debug purposes only. That
  // is, there is no mechanism to get access to a VMO via this name.
  fbl::Name<ZX_MAX_NAME_LEN> name_;

  static zx_status_t RoundSize(uint64_t size, uint64_t* out_size);

  static constexpr uint64_t MAX_SIZE = VmPageList::MAX_SIZE;
  // Ensure that MAX_SIZE + PAGE_SIZE doesn't overflow so no VmObjects
  // need to worry about overflow for loop bounds.
  static_assert(MAX_SIZE <= ROUNDDOWN(UINT64_MAX, PAGE_SIZE) - PAGE_SIZE);
  static_assert(MAX_SIZE % PAGE_SIZE == 0);

 private:
  mutable DECLARE_MUTEX(VmObject) child_observer_lock_;

  // This member, if not null, is used to signal the user facing Dispatcher.
  VmObjectChildObserver* child_observer_ TA_GUARDED(child_observer_lock_) = nullptr;

  using GlobalList = fbl::TaggedDoublyLinkedList<VmObject*, internal::GlobalListTag>;

  DECLARE_SINGLETON_CRITICAL_MUTEX(AllVmosLock);
  static GlobalList all_vmos_ TA_GUARDED(AllVmosLock::Get());

  using Cursor = VmoCursor<VmObject, AllVmosLock, GlobalList, GlobalList::iterator>;

  static fbl::DoublyLinkedList<Cursor*> all_vmos_cursors_ TA_GUARDED(AllVmosLock::Get());
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_VM_OBJECT_H_
