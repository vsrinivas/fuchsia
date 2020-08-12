// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_VM_OBJECT_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_VM_OBJECT_H_

#include <align.h>
#include <lib/user_copy/user_iovec.h>
#include <lib/user_copy/user_ptr.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stdint.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

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
#include <vm/page.h>
#include <vm/vm.h>
#include <vm/vm_page_list.h>

class VmMapping;
class PageRequest;
class VmObjectPaged;
class VmAspace;

typedef zx_status_t (*vmo_lookup_fn_t)(void* context, size_t offset, size_t index, paddr_t pa);

class VmObjectChildObserver {
 public:
  virtual void OnZeroChild() = 0;
  virtual void OnOneChild() = 0;
};

typedef struct vm_lock : fbl::RefCounted<struct vm_lock> {
  DECLARE_MUTEX(struct vm_lock) lock;
} vm_lock_t;

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
struct RangeChangeListTag {};
}  // namespace internal

// The base vm object that holds a range of bytes of data
//
// Can be created without mapping and used as a container of data, or mappable
// into an address space via VmAddressRegion::CreateVmMapping
class VmObject : public fbl::RefCountedUpgradeable<VmObject>,
                 public fbl::ContainableBaseClasses<
                     fbl::TaggedDoublyLinkedListable<VmObject*, internal::ChildListTag>,
                     fbl::TaggedDoublyLinkedListable<VmObject*, internal::GlobalListTag>,
                     fbl::TaggedSinglyLinkedListable<VmObject*, internal::RangeChangeListTag>> {
 public:
  // public API
  virtual zx_status_t Resize(uint64_t size) { return ZX_ERR_NOT_SUPPORTED; }

  virtual uint64_t size() const { return 0; }
  virtual uint32_t create_options() const { return 0; }

  // Returns true if the object is backed by RAM.
  virtual bool is_paged() const { return false; }
  // Returns true if the object is backed by a contiguous range of physical
  // memory.
  virtual bool is_contiguous() const { return false; }
  // Returns true if the object size can be changed.
  virtual bool is_resizable() const { return false; }
  // Returns true if the VMO was created vma CreatePagerVmo().
  virtual bool is_pager_backed() const { return false; }

  // Returns true if the vmo is a hidden paged vmo.
  virtual bool is_hidden() const { return false; }

  // Returns the number of physical pages currently attributed to the
  // object where (offset <= page_offset < offset+len).
  // |offset| and |len| are in bytes.
  virtual size_t AttributedPagesInRange(uint64_t offset, uint64_t len) const { return 0; }
  // Returns the number of physical pages currently attributed to the object.
  size_t AttributedPages() const { return AttributedPagesInRange(0, size()); }

  // find physical pages to back the range of the object
  virtual zx_status_t CommitRange(uint64_t offset, uint64_t len) { return ZX_ERR_NOT_SUPPORTED; }

  // find physical pages to back the range of the object and pin them.
  // |len| must be non-zero.
  virtual zx_status_t CommitRangePinned(uint64_t offset, uint64_t len) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // free a range of the vmo back to the default state
  virtual zx_status_t DecommitRange(uint64_t offset, uint64_t len) { return ZX_ERR_NOT_SUPPORTED; }

  // Zero a range of the VMO. May release physical pages in the process.
  virtual zx_status_t ZeroRange(uint64_t offset, uint64_t len) { return ZX_ERR_NOT_SUPPORTED; }

  // Unpin the given range of the vmo.  This asserts if it tries to unpin a
  // page that is already not pinned (do not expose this function to
  // usermode).
  virtual void Unpin(uint64_t offset, uint64_t len) {
    panic("Unpin should only be called on a pinned range");
  }

  // read/write operators against kernel pointers only
  virtual zx_status_t Read(void* ptr, uint64_t offset, size_t len) { return ZX_ERR_NOT_SUPPORTED; }
  virtual zx_status_t Write(const void* ptr, uint64_t offset, size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // execute lookup_fn on a given range of physical addresses within the vmo
  virtual zx_status_t Lookup(uint64_t offset, uint64_t len, vmo_lookup_fn_t lookup_fn,
                             void* context) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // read/write operators against user space pointers only
  virtual zx_status_t ReadUser(VmAspace* current_aspace, user_out_ptr<char> ptr, uint64_t offset,
                               size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  virtual zx_status_t ReadUserVector(VmAspace* current_aspace, user_out_iovec_t vec,
                                     uint64_t offset, size_t len);
  virtual zx_status_t WriteUser(VmAspace* current_aspace, user_in_ptr<const char> ptr,
                                uint64_t offset, size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  virtual zx_status_t WriteUserVector(VmAspace* current_aspace, user_in_iovec_t vec,
                                      uint64_t offset, size_t len);

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

  // cache maintenance operations.
  zx_status_t InvalidateCache(const uint64_t offset, const uint64_t len);
  zx_status_t CleanCache(const uint64_t offset, const uint64_t len);
  zx_status_t CleanInvalidateCache(const uint64_t offset, const uint64_t len);
  zx_status_t SyncCache(const uint64_t offset, const uint64_t len);

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

  // Number of pages that have been evicted over the lifetime of this VMO. Evicted counts for any
  // decommit style event such as user pager eviction or zero page merging.
  virtual uint64_t EvictedPagedCount() const { return 0; }

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
  //
  // TODO: Currently the caller can also pass null if it knows that the vm object has no
  // page source. This will no longer be the case once page allocations can be delayed.
  zx_status_t GetPage(uint64_t offset, uint pf_flags, list_node* free_list,
                      PageRequest* page_request, vm_page_t** page, paddr_t* pa) {
    Guard<Mutex> guard{&lock_};
    return GetPageLocked(offset, pf_flags, free_list, page_request, page, pa);
  }

  // See VmObject::GetPage
  virtual zx_status_t GetPageLocked(uint64_t offset, uint pf_flags, list_node* free_list,
                                    PageRequest* page_request, vm_page_t** page, paddr_t* pa)
      TA_REQ(lock_) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  Lock<Mutex>* lock() const TA_RET_CAP(lock_) { return &lock_; }
  Lock<Mutex>& lock_ref() const TA_RET_CAP(lock_) { return lock_; }

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
  bool AddChildLocked(VmObject* r) TA_REQ(lock_);

  // Notifies the child observer that there is one child.
  void NotifyOneChild() TA_EXCL(lock_);

  // Removes the child |child| from this vmo.
  //
  // Subclasses which override this function should be sure that ::DropChildLocked
  // and ::OnUserChildRemoved are called where appropraite.
  //
  // |guard| must be this vmo's lock.
  virtual void RemoveChild(VmObject* child, Guard<Mutex>&& guard) TA_REQ(lock_);

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
  void OnUserChildRemoved(Guard<Mutex>&& guard) TA_REQ(lock_);

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
    Guard<Mutex> guard{AllVmosLock::Get()};
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

  // Walks through every VMO, calls ScanForZeroPages on them, and returns the sum. This function is
  // very expensive and will hold the AllVmosLock mutex for the entire duration. Should not be
  // called casually or when it is not suitable to block operations against all other VMOs for an
  // extended period of time.
  static uint32_t ScanAllForZeroPages(bool reclaim);

  // Calls |HarvestAccessedBits| for every VMO in the system. Each individual call to
  // |HarvestAccessedBits| occurs without the all vmos lock being held, so VMOs may be added/removed
  // over the course of this operation.
  static void HarvestAllAccessedBits();

  // Scans for pages that could validly be de-duped/decommitted back to the zero page. If `reclaim`
  // is true the pages will actually be de-duped. In either case the number of found pages is
  // returned. It is expected that this would hold the VMOs lock for an extended period of time
  // and should only be called when it is suitable for block all VMO operations for an extended
  // period of time.
  virtual uint32_t ScanForZeroPages(bool reclaim) { return 0; }

  // Instructs the VMO to harvest any accessed bits in its mappings and update any meta data for
  // page age etc. This is allowed to be a no-op, and doesn't promise to generate any observable
  // results.
  virtual void HarvestAccessedBits() {}

  // Asks the VMO to attempt to evict the specified page. This returns true if the page was
  // actually from this VMO and was successfully evicted, at which point the caller now has
  // ownership of the page. Otherwise eviction is allowed to fail for any reason, specifically
  // if the page is considered in use, or the VMO has no way to recreate the page then eviction
  // will fail. Although eviction may fail for any reason, if it does the caller is able to assume
  // that either the page was not from this vmo, or that the page is not in any evictable page queue
  // (such as the pager_backed_ queue).
  virtual bool EvictPage(vm_page_t* page, uint64_t offset) { return false; }

 protected:
  explicit VmObject(fbl::RefPtr<vm_lock_t> root_lock);

  // private destructor, only called from refptr
  virtual ~VmObject();
  friend fbl::RefPtr<VmObject>;

  // Types for an additional linked list over the VmObject for use when doing a RangeChangeUpdate.
  //
  // To avoid unbounded stack growth we need to reserve the memory to exist on a
  // RangeChange list in our object so that we can have a flat iteration over a
  // work list. RangeChangeLists should only be used by the RangeChangeUpdate
  // code.
  using RangeChangeList = fbl::TaggedSinglyLinkedList<VmObject*, internal::RangeChangeListTag>;

  uint64_t range_change_offset_ TA_GUARDED(lock_);
  uint64_t range_change_len_ TA_GUARDED(lock_);

  DISALLOW_COPY_ASSIGN_AND_MOVE(VmObject);

  void AddToGlobalList();
  void RemoveFromGlobalList();

  // Different operations that RangeChangeUpdate* can perform against any VmMappings that are found.
  enum class RangeChangeOp {
    Unmap,
    RemoveWrite,
  };

  // Apply the specified operation to all mappings in the given range. This is applied to all
  // descendants within the range.
  void RangeChangeUpdateLocked(uint64_t offset, uint64_t len, RangeChangeOp op) TA_REQ(lock_);

  // Given an initial list of VmObject's performs RangeChangeUpdate on it until the list is empty.
  static void RangeChangeUpdateListLocked(RangeChangeList* list, RangeChangeOp op);

  // magic value
  fbl::Canary<fbl::magic("VMO_")> canary_;

  // The lock which protects this class. All VmObjects in a clone tree
  // share the same lock.
  Lock<Mutex>& lock_;
  // Pointer to the actual lock.
  fbl::RefPtr<vm_lock_t> lock_ptr_;

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
  // perform a cache maintenance operation against the vmo.
  enum class CacheOpType { Invalidate, Clean, CleanInvalidate, Sync };
  zx_status_t CacheOp(const uint64_t offset, const uint64_t len, const CacheOpType type);

  mutable DECLARE_MUTEX(VmObject) child_observer_lock_;

  // This member, if not null, is used to signal the user facing Dispatcher.
  VmObjectChildObserver* child_observer_ TA_GUARDED(child_observer_lock_) = nullptr;

  using GlobalList = fbl::TaggedDoublyLinkedList<VmObject*, internal::GlobalListTag>;

  DECLARE_SINGLETON_MUTEX(AllVmosLock);
  static GlobalList all_vmos_ TA_GUARDED(AllVmosLock::Get());

  // Cursor to allow for walking the all vmos list without needing to hold the AllVmosLock all the
  // time. In practice at most one cursor is expected to exist, but as the cursor list is global
  // the overhead of being generic to support multiple cursors is negligible.
  class VmoCursor : public fbl::DoublyLinkedListable<VmoCursor*> {
   public:
    VmoCursor() TA_REQ(AllVmosLock::Get());
    ~VmoCursor() TA_REQ(AllVmosLock::Get());

    // Advance the cursor and return the next VmObject or nullptr if at the end of the list.
    //
    // Once |Next| has returned nullptr, all subsequent calls will return nullptr.
    //
    // The caller must hold the |AllVmosLock|.
    VmObject* Next() TA_REQ(AllVmosLock::Get());

    // If the next element is |h|, advance the cursor past it.
    //
    // The caller must hold the |AllVmosLock|.
    void AdvanceIf(const VmObject* h) TA_REQ(AllVmosLock::Get());

   private:
    VmoCursor(const VmoCursor&) = delete;
    VmoCursor& operator=(const VmoCursor&) = delete;
    VmoCursor(VmoCursor&&) = delete;
    VmoCursor& operator=(VmoCursor&&) = delete;

    GlobalList::iterator iter_ TA_GUARDED(AllVmosLock::Get());
  };

  static fbl::DoublyLinkedList<VmoCursor*> all_vmos_cursors_ TA_GUARDED(AllVmosLock::Get());

  bool InGlobalList() { return fbl::InContainer<internal::GlobalListTag>(*this); }
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_VM_OBJECT_H_
