// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <kernel/mutex.h>
#include <kernel/vm.h>
#include <lib/user_copy/user_ptr.h>
#include <list.h>
#include <magenta/thread_annotations.h>
#include <fbl/array.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <stdint.h>
#include <vm/pmm.h>
#include <vm/vm_object.h>
#include <vm/vm_page_list.h>

// the main VM object type, holding a list of pages
class VmObjectPaged final : public VmObject {
public:
    static status_t Create(uint32_t pmm_alloc_flags, uint64_t size, fbl::RefPtr<VmObject>* vmo);

    static status_t CreateFromROData(const void* data, size_t size, fbl::RefPtr<VmObject>* vmo);

    status_t Resize(uint64_t size) override;
    status_t ResizeLocked(uint64_t size) override TA_REQ(lock_);
    uint64_t size() const override
        // TODO: Figure out whether it's safe to lock here without causing
        // any deadlocks.
        TA_NO_THREAD_SAFETY_ANALYSIS { return size_; }
    bool is_paged() const override { return true; }

    size_t AllocatedPagesInRange(uint64_t offset, uint64_t len) const override;

    status_t CommitRange(uint64_t offset, uint64_t len, uint64_t* committed) override;
    status_t CommitRangeContiguous(uint64_t offset, uint64_t len, uint64_t* committed,
                                   uint8_t alignment_log2) override;
    status_t DecommitRange(uint64_t offset, uint64_t len, uint64_t* decommitted) override;

    status_t Pin(uint64_t offset, uint64_t len) override;
    void Unpin(uint64_t offset, uint64_t len) override;

    status_t Read(void* ptr, uint64_t offset, size_t len, size_t* bytes_read) override;
    status_t Write(const void* ptr, uint64_t offset, size_t len, size_t* bytes_written) override;
    status_t Lookup(uint64_t offset, uint64_t len, uint pf_flags,
                    vmo_lookup_fn_t lookup_fn, void* context) override;

    status_t ReadUser(user_ptr<void> ptr, uint64_t offset, size_t len,
                      size_t* bytes_read) override;
    status_t WriteUser(user_ptr<const void> ptr, uint64_t offset, size_t len,
                       size_t* bytes_written) override;

    status_t LookupUser(uint64_t offset, uint64_t len, user_ptr<paddr_t> buffer,
                        size_t buffer_size) override;

    void Dump(uint depth, bool verbose) override;

    status_t InvalidateCache(const uint64_t offset, const uint64_t len) override;
    status_t CleanCache(const uint64_t offset, const uint64_t len) override;
    status_t CleanInvalidateCache(const uint64_t offset, const uint64_t len) override;
    status_t SyncCache(const uint64_t offset, const uint64_t len) override;

    status_t GetPageLocked(uint64_t offset, uint pf_flags, list_node* free_list,
                           vm_page_t**, paddr_t*) override
        // Calls a Locked method of the parent, which confuses analysis.
        TA_NO_THREAD_SAFETY_ANALYSIS;

    status_t CloneCOW(uint64_t offset, uint64_t size, bool copy_name,
                      fbl::RefPtr<VmObject>* clone_vmo) override
        // Calls a Locked method of the child, which confuses analysis.
        TA_NO_THREAD_SAFETY_ANALYSIS;

    void RangeChangeUpdateFromParentLocked(uint64_t offset, uint64_t len) override
        // Called under the parent's lock, which confuses analysis.
        TA_NO_THREAD_SAFETY_ANALYSIS;

private:
    // private constructor (use Create())
    explicit VmObjectPaged(uint32_t pmm_alloc_flags, fbl::RefPtr<VmObject> parent);

    // private destructor, only called from refptr
    ~VmObjectPaged() override;
    friend fbl::RefPtr<VmObjectPaged>;

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmObjectPaged);

    // perform a cache maintenance operation against the vmo.
    enum class CacheOpType { Invalidate,
                             Clean,
                             CleanInvalidate,
                             Sync };
    status_t CacheOp(const uint64_t offset, const uint64_t len, const CacheOpType type);

    // add a page to the object
    status_t AddPage(vm_page_t* p, uint64_t offset);
    status_t AddPageLocked(vm_page_t* p, uint64_t offset) TA_REQ(lock_);

    // internal page list routine
    void AddPageToArray(size_t index, vm_page_t* p);

    status_t PinLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);
    void UnpinLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);

    // internal check if any pages in a range are pinned
    bool AnyPagesPinnedLocked(uint64_t offset, size_t len) TA_REQ(lock_);

    // internal read/write routine that takes a templated copy function to help share some code
    template <typename T>
    status_t ReadWriteInternal(uint64_t offset, size_t len, size_t* bytes_copied, bool write,
                               T copyfunc);

    // set our offset within our parent
    status_t SetParentOffsetLocked(uint64_t o) TA_REQ(lock_);

    // maximum size of a VMO is one page less than the full 64bit range
    static const uint64_t MAX_SIZE = ROUNDDOWN(UINT64_MAX, PAGE_SIZE);

    // members
    uint64_t size_ TA_GUARDED(lock_) = 0;
    uint64_t parent_offset_ TA_GUARDED(lock_) = 0;
    uint32_t pmm_alloc_flags_ TA_GUARDED(lock_) = PMM_ALLOC_FLAG_ANY;

    // a tree of pages
    VmPageList page_list_ TA_GUARDED(lock_);
};
