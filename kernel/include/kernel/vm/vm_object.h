// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <kernel/mutex.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_page_list.h>
#include <lib/user_copy/user_ptr.h>
#include <list.h>
#include <magenta/thread_annotations.h>
#include <mxtl/array.h>
#include <mxtl/canary.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/macros.h>
#include <mxtl/name.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <stdint.h>

class VmMapping;

typedef status_t (*vmo_lookup_fn_t)(void* context, size_t offset, size_t index, paddr_t pa);

// The base vm object that holds a range of bytes of data
//
// Can be created without mapping and used as a container of data, or mappable
// into an address space via VmAddressRegion::CreateVmMapping
class VmObject : public mxtl::RefCounted<VmObject>,
                 public mxtl::DoublyLinkedListable<VmObject*> {
public:
    // public API
    virtual status_t Resize(uint64_t size) { return ERR_NOT_SUPPORTED; }
    virtual status_t ResizeLocked(uint64_t size) TA_REQ(lock_) { return ERR_NOT_SUPPORTED; }

    virtual uint64_t size() const { return 0; }

    // Returns the number of physical pages currently allocated to the
    // object where (offset <= page_offset < offset+len).
    // |offset| and |len| are in bytes.
    virtual size_t AllocatedPagesInRange(uint64_t offset, uint64_t len) const {
        return 0;
    }
    // Returns the number of physical pages currently allocated to the object.
    size_t AllocatedPages() const {
        return AllocatedPagesInRange(0, size());
    }

    // find physical pages to back the range of the object
    virtual status_t CommitRange(uint64_t offset, uint64_t len, uint64_t* committed) {
        return ERR_NOT_SUPPORTED;
    }

    // find a contiguous run of physical pages to back the range of the object
    virtual status_t CommitRangeContiguous(uint64_t offset, uint64_t len, uint64_t* committed,
                                           uint8_t alignment_log2) {
        return ERR_NOT_SUPPORTED;
    }

    // free a range of the vmo back to the default state
    virtual status_t DecommitRange(uint64_t offset, uint64_t len, uint64_t* decommitted) {
        return ERR_NOT_SUPPORTED;
    }

    // read/write operators against kernel pointers only
    virtual status_t Read(void* ptr, uint64_t offset, size_t len, size_t* bytes_read) {
        return ERR_NOT_SUPPORTED;
    }
    virtual status_t Write(const void* ptr, uint64_t offset, size_t len, size_t* bytes_written) {
        return ERR_NOT_SUPPORTED;
    }

    // execute lookup_fn on a given range of physical addresses within the vmo
    virtual status_t Lookup(uint64_t offset, uint64_t len, uint pf_flags,
                            vmo_lookup_fn_t lookup_fn, void* context) {
        return ERR_NOT_SUPPORTED;
    }

    // read/write operators against user space pointers only
    virtual status_t ReadUser(user_ptr<void> ptr, uint64_t offset, size_t len, size_t* bytes_read) {
        return ERR_NOT_SUPPORTED;
    }
    virtual status_t WriteUser(user_ptr<const void> ptr, uint64_t offset, size_t len,
                               size_t* bytes_written) {
        return ERR_NOT_SUPPORTED;
    }

    // translate a range of the vmo to physical addresses and store in the buffer
    virtual status_t LookupUser(uint64_t offset, uint64_t len, user_ptr<paddr_t> buffer,
                                size_t buffer_size) {
        return ERR_NOT_SUPPORTED;
    }

    // Returns a null-terminated name, or the empty string if set_name() has not
    // been called.
    void get_name(char out_name[MX_MAX_NAME_LEN]) const;

    // Sets the name of the object. May truncate internally. |len| is the size
    // of the buffer pointed to by |name|.
    status_t set_name(const char* name, size_t len);

    // Returns a user ID associated with this VMO, or zero.
    // Typically used to hold a magenta koid for Dispatcher-wrapped VMOs.
    uint64_t user_id() const;

    // Returns the parent's user_id() if this VMO has a parent,
    // otherwise returns zero.
    uint64_t parent_user_id() const;

    // Sets the value returned by |user_id()|. May only be called once.
    void set_user_id(uint64_t user_id);

    virtual void Dump(uint depth, bool verbose) = 0;

    // cache maintainence operations.
    virtual status_t InvalidateCache(const uint64_t offset, const uint64_t len) {
        return ERR_NOT_SUPPORTED;
    }
    virtual status_t CleanCache(const uint64_t offset, const uint64_t len) {
        return ERR_NOT_SUPPORTED;
    }
    virtual status_t CleanInvalidateCache(const uint64_t offset, const uint64_t len) {
        return ERR_NOT_SUPPORTED;
    }
    virtual status_t SyncCache(const uint64_t offset, const uint64_t len) {
        return ERR_NOT_SUPPORTED;
    }

    virtual status_t GetMappingCachePolicy(uint32_t* cache_policy) {
        return ERR_NOT_SUPPORTED;
    }

    virtual status_t SetMappingCachePolicy(const uint32_t cache_policy) {
        return ERR_NOT_SUPPORTED;
    }

    // create a copy-on-write clone vmo at the page-aligned offset and length
    // note: it's okay to start or extend past the size of the parent
    virtual status_t CloneCOW(uint64_t offset, uint64_t size, mxtl::RefPtr<VmObject>* clone_vmo) {
        return ERR_NOT_SUPPORTED;
    }

    // Returns true if this VMO was created via CloneCOW().
    // TODO: If more types of clones appear, replace this with a method that
    // returns an enum rather than adding a new method for each clone type.
    bool is_cow_clone() const;

    // get a pointer to the page structure and/or physical address at the specified offset.
    // valid flags are VMM_PF_FLAG_*
    virtual status_t GetPageLocked(uint64_t offset, uint pf_flags,
                                   vm_page_t** page, paddr_t* pa) TA_REQ(lock_) {
        return ERR_NOT_SUPPORTED;
    }

    Mutex* lock() TA_RET_CAP(lock_) { return &lock_; }
    Mutex& lock_ref() TA_RET_CAP(lock_) { return lock_; }

    void AddMappingLocked(VmMapping* r) TA_REQ(lock_);
    void RemoveMappingLocked(VmMapping* r) TA_REQ(lock_);
    uint32_t num_mappings() const;

    // Returns an estimate of the number of unique VmAspaces that this object
    // is mapped into.
    uint32_t share_count() const;

    void AddChildLocked(VmObject* r) TA_REQ(lock_);
    void RemoveChildLocked(VmObject* r) TA_REQ(lock_);
    uint32_t num_children() const;

protected:
    // private constructor (use Create())
    explicit VmObject(mxtl::RefPtr<VmObject> parent);
    VmObject()
        : VmObject(nullptr) {}

    // private destructor, only called from refptr
    virtual ~VmObject();
    friend mxtl::RefPtr<VmObject>;

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmObject);

    // inform all mappings and children that a range of this vmo's pages were added or removed.
    void RangeChangeUpdateLocked(uint64_t offset, uint64_t len) TA_REQ(lock_);

    // above call but called from a parent
    virtual void RangeChangeUpdateFromParentLocked(uint64_t offset, uint64_t len)
        // Called under the parent's lock, which confuses analysis.
        TA_NO_THREAD_SAFETY_ANALYSIS { RangeChangeUpdateLocked(offset, len); }

    // magic value
    mxtl::Canary<mxtl::magic("VMO_")> canary_;

    // members

    // declare a local mutex and default to pointing at it
    // if constructed with a parent vmo, point lock_ at the parent's lock
private:
    Mutex local_lock_;

protected:
    Mutex& lock_;

    // list of every mapping
    mxtl::DoublyLinkedList<VmMapping*> mapping_list_ TA_GUARDED(lock_);

    // list of every child
    mxtl::DoublyLinkedList<VmObject*> children_list_ TA_GUARDED(lock_);

    // parent pointer (may be null)
    mxtl::RefPtr<VmObject> parent_ TA_GUARDED(lock_);

    // lengths of corresponding lists
    uint32_t mapping_list_len_ TA_GUARDED(lock_) = 0;
    uint32_t children_list_len_ TA_GUARDED(lock_) = 0;

    uint64_t user_id_ TA_GUARDED(lock_) = 0;

    // The user-friendly VMO name. For debug purposes only. That
    // is, there is no mechanism to get access to a VMO via this name.
    mxtl::Name<MX_MAX_NAME_LEN> name_;
};
