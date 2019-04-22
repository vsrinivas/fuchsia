// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <fbl/array.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <kernel/mutex.h>
#include <lib/user_copy/user_ptr.h>
#include <list.h>
#include <stdint.h>
#include <vm/page_source.h>
#include <vm/pmm.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <vm/vm_page_list.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

// the main VM object type, holding a list of pages
class VmObjectPaged final : public VmObject {
public:
    // |options_| is a bitmask of:
    static constexpr uint32_t kResizable = (1u << 0);
    static constexpr uint32_t kContiguous = (1u << 1);

    static zx_status_t Create(uint32_t pmm_alloc_flags,
                              uint32_t options,
                              uint64_t size, fbl::RefPtr<VmObject>* vmo);

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

    static zx_status_t CreateExternal(fbl::RefPtr<PageSource> src, uint32_t options,
                                      uint64_t size, fbl::RefPtr<VmObject>* vmo);

    zx_status_t Resize(uint64_t size) override;
    uint32_t create_options() const override { return options_; }
    uint64_t size() const override
        // TODO: Figure out whether it's safe to lock here without causing
        // any deadlocks.
        TA_NO_THREAD_SAFETY_ANALYSIS { return size_; }
    bool is_paged() const override { return true; }
    bool is_contiguous() const override { return (options_ & kContiguous); }
    bool is_resizable() const override { return (options_ & kResizable); }
    bool is_pager_backed() const override {
        Guard<fbl::Mutex> guard{&lock_};
        return GetRootPageSourceLocked() != nullptr;
    }
    ChildType child_type() const override {
        Guard<fbl::Mutex> guard{&lock_};
        return parent_ ? ChildType::kCowClone : ChildType::kNotChild;
    }

    size_t AllocatedPagesInRange(uint64_t offset, uint64_t len) const override;

    zx_status_t CommitRange(uint64_t offset, uint64_t len) override;
    zx_status_t DecommitRange(uint64_t offset, uint64_t len) override;

    zx_status_t Pin(uint64_t offset, uint64_t len) override;
    void Unpin(uint64_t offset, uint64_t len) override;

    zx_status_t Read(void* ptr, uint64_t offset, size_t len) override;
    zx_status_t Write(const void* ptr, uint64_t offset, size_t len) override;
    zx_status_t Lookup(uint64_t offset, uint64_t len,
                       vmo_lookup_fn_t lookup_fn, void* context) override;

    zx_status_t ReadUser(user_out_ptr<void> ptr, uint64_t offset, size_t len) override;
    zx_status_t WriteUser(user_in_ptr<const void> ptr, uint64_t offset, size_t len) override;

    zx_status_t TakePages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) override;
    zx_status_t SupplyPages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) override;

    void Dump(uint depth, bool verbose) override;

    zx_status_t InvalidateCache(const uint64_t offset, const uint64_t len) override;
    zx_status_t CleanCache(const uint64_t offset, const uint64_t len) override;
    zx_status_t CleanInvalidateCache(const uint64_t offset, const uint64_t len) override;
    zx_status_t SyncCache(const uint64_t offset, const uint64_t len) override;

    zx_status_t GetPageLocked(uint64_t offset, uint pf_flags, list_node* free_list,
                              PageRequest* page_request, vm_page_t**, paddr_t*) override
        // Calls a Locked method of the parent, which confuses analysis.
        TA_NO_THREAD_SAFETY_ANALYSIS;

    zx_status_t CreateCowClone(bool resizable, uint64_t offset, uint64_t size, bool copy_name,
                               fbl::RefPtr<VmObject>* child_vmo) override
        // Calls a Locked method of the child, which confuses analysis.
        TA_NO_THREAD_SAFETY_ANALYSIS;

    void RangeChangeUpdateFromParentLocked(uint64_t offset, uint64_t len) override
        // Called under the parent's lock, which confuses analysis.
        TA_NO_THREAD_SAFETY_ANALYSIS;

    uint32_t GetMappingCachePolicy() const override;
    zx_status_t SetMappingCachePolicy(const uint32_t cache_policy) override;

    void DetachSource() override {
        DEBUG_ASSERT(page_source_);
        page_source_->Detach();
    }

    // The size is clamped to allow VmPageList to use a one-past-the-end for
    // VmPageListNode offsets.
    static const uint64_t MAX_SIZE = ROUNDDOWN(UINT64_MAX, VmPageListNode::kPageFanOut * PAGE_SIZE);

private:
    // private constructor (use Create())
    VmObjectPaged(
        uint32_t options, uint32_t pmm_alloc_flags, uint64_t size,
        fbl::RefPtr<VmObject> parent, fbl::RefPtr<PageSource> page_source);

    // private destructor, only called from refptr
    ~VmObjectPaged() override;
    friend fbl::RefPtr<VmObjectPaged>;

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmObjectPaged);

    // perform a cache maintenance operation against the vmo.
    enum class CacheOpType { Invalidate,
                             Clean,
                             CleanInvalidate,
                             Sync
    };
    zx_status_t CacheOp(const uint64_t offset, const uint64_t len, const CacheOpType type);

    // add a page to the object
    zx_status_t AddPage(vm_page_t* p, uint64_t offset);
    zx_status_t AddPageLocked(vm_page_t* p, uint64_t offset) TA_REQ(lock_);

    // internal page list routine
    void AddPageToArray(size_t index, vm_page_t* p);

    zx_status_t PinLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);
    void UnpinLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);

    fbl::RefPtr<PageSource> GetRootPageSourceLocked() const
        // Walks the parent chain to get the root page source, which confuses analysis.
        TA_NO_THREAD_SAFETY_ANALYSIS;

    // internal check if any pages in a range are pinned
    bool AnyPagesPinnedLocked(uint64_t offset, size_t len) TA_REQ(lock_);

    // see AllocatedPagesInRange
    size_t AllocatedPagesInRangeLocked(uint64_t offset, uint64_t len) const TA_REQ(lock_);

    // internal read/write routine that takes a templated copy function to help share some code
    template <typename T>
    zx_status_t ReadWriteInternal(uint64_t offset, size_t len, bool write, T copyfunc);

    // set our offset within our parent
    zx_status_t SetParentOffsetLocked(uint64_t o) TA_REQ(lock_);

    // members
    const uint32_t options_;
    uint64_t size_ TA_GUARDED(lock_) = 0;
    uint64_t parent_offset_ TA_GUARDED(lock_) = 0;
    uint32_t pmm_alloc_flags_ TA_GUARDED(lock_) = PMM_ALLOC_FLAG_ANY;
    uint32_t cache_policy_ TA_GUARDED(lock_) = ARCH_MMU_FLAG_CACHED;

    // The page source, if any.
    const fbl::RefPtr<PageSource> page_source_;

    // a tree of pages
    VmPageList page_list_ TA_GUARDED(lock_);
};
