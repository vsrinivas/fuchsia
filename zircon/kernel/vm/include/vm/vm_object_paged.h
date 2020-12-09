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
#include <vm/vm_cow_pages.h>
#include <vm/vm_object.h>

// the main VM object type, based on a copy-on-write set of pages.
class VmObjectPaged final : public VmObject {
 public:
  // |options_| is a bitmask of:
  static constexpr uint32_t kResizable = (1u << 0);
  static constexpr uint32_t kContiguous = (1u << 1);
  static constexpr uint32_t kSlice = (1u << 3);

  static zx_status_t Create(uint32_t pmm_alloc_flags, uint32_t options, uint64_t size,
                            fbl::RefPtr<VmObjectPaged>* vmo);

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
  uint64_t size() const override TA_EXCL(lock_) {
    Guard<Mutex> guard{&lock_};
    return cow_pages_locked()->size_locked();
  }
  bool is_paged() const override { return true; }
  bool is_contiguous() const override { return (options_ & kContiguous); }
  bool is_resizable() const override { return (options_ & kResizable); }
  bool is_pager_backed() const override {
    Guard<Mutex> guard{&lock_};
    return cow_pages_locked()->is_pager_backed_locked();
  }
  ChildType child_type() const override {
    if (is_slice()) {
      return ChildType::kSlice;
    }
    Guard<Mutex> guard{&lock_};
    return parent_ ? ChildType::kCowClone : ChildType::kNotChild;
  }
  bool is_slice() const { return options_ & kSlice; }
  uint64_t parent_user_id() const override {
    Guard<Mutex> guard{&lock_};
    if (parent_) {
      AssertHeld(parent_->lock_ref());
      return parent_->user_id_locked();
    }
    return 0;
  }
  void set_user_id(uint64_t user_id) override {
    VmObject::set_user_id(user_id);
    Guard<Mutex> guard{&lock_};
    cow_pages_locked()->set_page_attribution_user_id_locked(user_id);
  }

  uint64_t HeapAllocationBytes() const override {
    Guard<Mutex> guard{&lock_};
    return cow_pages_locked()->HeapAllocationBytesLocked();
  }

  uint64_t EvictionEventCount() const override {
    Guard<Mutex> guard{&lock_};

    return cow_pages_locked()->EvictionEventCountLocked();
  }

  size_t AttributedPagesInRange(uint64_t offset, uint64_t len) const override {
    Guard<Mutex> guard{&lock_};
    return AttributedPagesInRangeLocked(offset, len);
  }

  zx_status_t CommitRange(uint64_t offset, uint64_t len) override {
    return CommitRangeInternal(offset, len, false);
  }
  zx_status_t CommitRangePinned(uint64_t offset, uint64_t len) override {
    return CommitRangeInternal(offset, len, true);
  }
  zx_status_t DecommitRange(uint64_t offset, uint64_t len) override;
  zx_status_t ZeroRange(uint64_t offset, uint64_t len) override;

  void Unpin(uint64_t offset, uint64_t len) override {
    Guard<Mutex> guard{&lock_};
    cow_pages_locked()->UnpinLocked(offset, len);
  }

  zx_status_t Read(void* ptr, uint64_t offset, size_t len) override;
  zx_status_t Write(const void* ptr, uint64_t offset, size_t len) override;
  zx_status_t Lookup(uint64_t offset, uint64_t len,
                     fbl::Function<zx_status_t(uint64_t offset, paddr_t pa)> lookup_fn) override;
  zx_status_t LookupContiguous(uint64_t offset, uint64_t len, paddr_t* out_paddr) override;

  zx_status_t ReadUser(VmAspace* current_aspace, user_out_ptr<char> ptr, uint64_t offset,
                       size_t len) override;
  zx_status_t WriteUser(VmAspace* current_aspace, user_in_ptr<const char> ptr, uint64_t offset,
                        size_t len) override;

  zx_status_t TakePages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) override;
  zx_status_t SupplyPages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) override;
  zx_status_t FailPageRequests(uint64_t offset, uint64_t len, zx_status_t error_status) override {
    Guard<Mutex> guard{&lock_};

    return cow_pages_locked()->FailPageRequestsLocked(offset, len, error_status);
  }

  void Dump(uint depth, bool verbose) override {
    Guard<Mutex> guard{&lock_};
    DumpLocked(depth, verbose);
  }

  zx_status_t GetPageLocked(uint64_t offset, uint pf_flags, list_node* alloc_list,
                            PageRequest* page_request, vm_page_t** out_page,
                            paddr_t* out_paddr) override TA_REQ(lock_) {
    return cow_pages_locked()->GetPageLocked(offset, pf_flags, alloc_list, page_request, out_page,
                                             out_paddr);
  }

  zx_status_t CreateClone(Resizability resizable, CloneType type, uint64_t offset, uint64_t size,
                          bool copy_name, fbl::RefPtr<VmObject>* child_vmo) override;

  uint32_t GetMappingCachePolicy() const override {
    Guard<Mutex> guard{&lock_};
    return GetMappingCachePolicyLocked();
  }
  uint32_t GetMappingCachePolicyLocked() const TA_REQ(lock_) { return cache_policy_; }
  zx_status_t SetMappingCachePolicy(const uint32_t cache_policy) override;

  void DetachSource() override {
    Guard<Mutex> guard{&lock_};

    cow_pages_locked()->DetachSource();
  }

  zx_status_t CreateChildSlice(uint64_t offset, uint64_t size, bool copy_name,
                               fbl::RefPtr<VmObject>* child_vmo) override;

  uint32_t ScanForZeroPages(bool reclaim) override;

  void HarvestAccessedBits() override;

  // Returns whether or not zero pages can be safely deduped from this VMO. Zero pages cannot be
  // deduped if the VMO is in use for kernel mappings, or if the pages cannot be accessed from the
  // physmap due to not being cached.
  bool CanDedupZeroPagesLocked() TA_REQ(lock_);

  // This performs a very expensive validation that checks if pages have been split correctly in
  // this VMO and is intended as a debugging aid. A return value of false indicates that the VMO
  // hierarchy is corrupt and the system should probably panic as soon as possible. As a result,
  // if false is returned this may write various additional information to the debuglog.
  bool DebugValidatePageSplits() const {
    Guard<Mutex> guard{&lock_};
    return cow_pages_locked()->DebugValidatePageSplitsLocked();
  }

  // Used to cache the page attribution count for this VMO. Also tracks the hierarchy generation
  // count at the time of caching the attributed page count.
  struct CachedPageAttribution {
    uint64_t generation_count = 0;
    size_t page_count = 0;
  };

  // Exposed for testing.
  CachedPageAttribution GetCachedPageAttribution() const {
    Guard<Mutex> guard{&lock_};
    return cached_page_attribution_;
  }

  // Called from VmMapping to cache page attribution counts.
  uint64_t GetHierarchyGenerationCount() const {
    Guard<Mutex> guard{&lock_};
    return GetHierarchyGenerationCountLocked();
  }

  // Exposed for testing.
  fbl::RefPtr<VmCowPages> DebugGetCowPages() const {
    Guard<Mutex> guard{&lock_};
    return cow_pages_;
  }

  using RangeChangeOp = VmCowPages::RangeChangeOp;
  // Apply the specified operation to all mappings in the given range.
  void RangeChangeUpdateLocked(uint64_t offset, uint64_t len, RangeChangeOp op) TA_REQ(lock_);

  // This is exposed so that VmCowPages can call it. It is used to update the VmCowPages object
  // that this VMO points to for its operations. When updating it must be set to a non-null
  // reference, and any mappings or pin operations must remain equivalently valid.
  // The previous cow pages references is returned so that the caller can perform sanity checks.
  fbl::RefPtr<VmCowPages> SetCowPagesReferenceLocked(fbl::RefPtr<VmCowPages> cow_pages)
      TA_REQ(lock_) {
    DEBUG_ASSERT(cow_pages);
    fbl::RefPtr<VmCowPages> ret = ktl::move(cow_pages_);
    cow_pages_ = ktl::move(cow_pages);
    return ret;
  }

  // Promote this VMO's pages for reclamation under memory pressure.
  // Currently used only for pager-backed VMOs to move their pages to the end of the
  // pager-backed queue, so that they can be evicted first.
  void PromoteForReclamation();

 private:
  // private constructor (use Create())
  VmObjectPaged(uint32_t options, fbl::RefPtr<VmHierarchyState> root_state);

  static zx_status_t CreateCommon(uint32_t pmm_alloc_flags, uint32_t options, uint64_t size,
                                  fbl::RefPtr<VmObjectPaged>* vmo);

  // private destructor, only called from refptr
  ~VmObjectPaged() override;
  friend fbl::RefPtr<VmObjectPaged>;

  DISALLOW_COPY_ASSIGN_AND_MOVE(VmObjectPaged);

  // Unified function that implements both CommitRange and CommitRangePinned
  zx_status_t CommitRangeInternal(uint64_t offset, uint64_t len, bool pin);

  // Internal decommit range helper that expects the lock to be held.
  zx_status_t DecommitRangeLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);

  // see AttributedPagesInRange
  size_t AttributedPagesInRangeLocked(uint64_t offset, uint64_t len) const TA_REQ(lock_);

  // internal read/write routine that takes a templated copy function to help share some code
  template <typename T>
  zx_status_t ReadWriteInternalLocked(uint64_t offset, size_t len, bool write, T copyfunc,
                                      Guard<Mutex>* guard) TA_REQ(lock_);

  // Zeroes a partial range in a page. May use CallUnlocked on the passed in guard. The page to zero
  // is looked up using page_base_offset, and will be committed if needed. The range of
  // [zero_start_offset, zero_end_offset) is relative to the page and so [0, PAGE_SIZE) would zero
  // the entire page.
  zx_status_t ZeroPartialPage(uint64_t page_base_offset, uint64_t zero_start_offset,
                              uint64_t zero_end_offset, Guard<Mutex>* guard) TA_REQ(lock_);

  // Internal implementations that assume lock is already held.
  void DumpLocked(uint depth, bool verbose) const TA_REQ(lock_);

  // Convenience wrapper that returns cow_pages_ whilst asserting that the lock is held.
  VmCowPages* cow_pages_locked() const TA_REQ(lock_) TA_ASSERT(cow_pages_locked()->lock()) {
    AssertHeld(cow_pages_->lock_ref());
    return cow_pages_.get();
  }

  uint64_t size_locked() const TA_REQ(lock_) { return cow_pages_locked()->size_locked(); }

  // members
  const uint32_t options_;
  uint32_t cache_policy_ TA_GUARDED(lock_) = ARCH_MMU_FLAG_CACHED;

  // parent pointer (may be null). This is a raw pointer as we have no need to hold our parent alive
  // once they want to go away.
  VmObjectPaged* parent_ TA_GUARDED(lock_) = nullptr;

  // Tracks the last cached page attribution count.
  mutable CachedPageAttribution cached_page_attribution_ TA_GUARDED(lock_) = {};

  // Our VmCowPages may be null during object initialization in the internal Create routines. As a
  // consequence if this is null it implies that the VMO is *not* in the global list. Otherwise it
  // can generally be assumed that this is non-null.
  fbl::RefPtr<VmCowPages> cow_pages_ TA_GUARDED(lock_);
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_VM_OBJECT_PAGED_H_
