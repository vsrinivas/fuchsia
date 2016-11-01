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
#include <mxtl/array.h>
#include <mxtl/macros.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <stdint.h>

// The base vm object that holds a range of bytes of data
//
// Can be created without mapping and used as a container of data, or mappable
// into an address space via VmAspace::MapObject

class VmObject : public mxtl::RefCounted<VmObject> {
public:
    virtual status_t Resize(uint64_t size) = 0;

    virtual uint64_t size() const = 0;
    virtual size_t AllocatedPages() const = 0;

    // find physical pages to back the range of the object
    virtual int64_t CommitRange(uint64_t offset, uint64_t len) = 0;

    // find a contiguous run of physical pages to back the range of the object
    virtual int64_t CommitRangeContiguous(uint64_t offset, uint64_t len, uint8_t alignment_log2 = 0) = 0;

    // get a pointer to a page at a given offset
    virtual vm_page_t* GetPage(uint64_t offset) = 0;

    // get the physical address of a page at offset
    virtual status_t GetPage(uint64_t offset, paddr_t* pa) {
        auto page = GetPage(offset);
        if (!page)
            return ERR_NOT_FOUND;
        *pa = vm_page_to_paddr(page);
        return NO_ERROR;
    }

    // fault in a page at a given offset with PF_FLAGS
    virtual vm_page_t* FaultPage(uint64_t offset, uint pf_flags) = 0;

    // fault in a page at a given offset with PF_FLAGS returning the physical address
    virtual status_t FaultPage(uint64_t offset, uint pf_flags, paddr_t* pa) {
        auto page = FaultPage(offset, pf_flags);
        if (!page)
            return ERR_NOT_FOUND;
        *pa = vm_page_to_paddr(page);
        return NO_ERROR;
    }

    // read/write operators against kernel pointers only
    virtual status_t Read(void* ptr, uint64_t offset, size_t len, size_t* bytes_read) = 0;
    virtual status_t Write(const void* ptr, uint64_t offset, size_t len, size_t* bytes_written) = 0;

    // read/write operators against user space pointers only
    virtual status_t ReadUser(user_ptr<void> ptr, uint64_t offset, size_t len, size_t* bytes_read) = 0;
    virtual status_t WriteUser(user_ptr<const void> ptr, uint64_t offset, size_t len, size_t* bytes_written) = 0;

    // translate a range of the vmo to physical addresses and store in the buffer
    virtual status_t Lookup(uint64_t offset, uint64_t len, user_ptr<paddr_t>, size_t) = 0;

    virtual void Dump(bool page_dump = false) = 0;

protected:
    // private constructor (use Create())
    VmObject();

    // private destructor, only called from refptr
    virtual ~VmObject();
    friend mxtl::RefPtr<VmObject>;

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmObject);

    // magic value
    static const uint32_t MAGIC = 0x564d4f5f; // VMO_
    uint32_t magic_ = MAGIC;

    // members
    mutable Mutex lock_;
};

// the main VM object type, holding a list of pages
class VmObjectPaged final : public VmObject {
public:
    static mxtl::RefPtr<VmObject> Create(uint32_t pmm_alloc_flags, uint64_t size);

    static mxtl::RefPtr<VmObject> CreateFromROData(const void* data,
                                                   size_t size);

    virtual status_t Resize(uint64_t size);

    virtual uint64_t size() const { return size_; }
    virtual size_t AllocatedPages() const;

    virtual int64_t CommitRange(uint64_t offset, uint64_t len);

    virtual int64_t CommitRangeContiguous(uint64_t offset, uint64_t len, uint8_t alignment_log2 = 0);

    virtual vm_page_t* GetPage(uint64_t offset);

    virtual vm_page_t* FaultPage(uint64_t offset, uint pf_flags);

    virtual status_t Read(void* ptr, uint64_t offset, size_t len, size_t* bytes_read);
    virtual status_t Write(const void* ptr, uint64_t offset, size_t len, size_t* bytes_written);

    virtual status_t ReadUser(user_ptr<void> ptr, uint64_t offset, size_t len, size_t* bytes_read);
    virtual status_t WriteUser(user_ptr<const void> ptr, uint64_t offset, size_t len, size_t* bytes_written);

    virtual status_t Lookup(uint64_t offset, uint64_t len, user_ptr<paddr_t>, size_t);

    virtual void Dump(bool page_dump = false);

private:
    // private constructor (use Create())
    explicit VmObjectPaged(uint32_t pmm_alloc_flags);

    // private destructor, only called from refptr
    virtual ~VmObjectPaged();

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmObjectPaged);

    // unlocked versions of the above routines
    vm_page_t* FaultPageLocked(uint64_t offset, uint pf_flags);
    vm_page_t* GetPageLocked(uint64_t offset);

    // add a page to the object
    status_t AddPage(vm_page_t* p, uint64_t offset);

    // internal page list routine
    void AddPageToArray(size_t index, vm_page_t* p);

    // internal read/write routine that takes a templated copy function to help share some code
    template <typename T>
    status_t ReadWriteInternal(uint64_t offset, size_t len, size_t* bytes_copied, bool write,
                               T copyfunc);

// constants
#if _LP64
    static const uint64_t MAX_SIZE = ROUNDDOWN(SIZE_MAX, PAGE_SIZE);
#else
    static const uint64_t MAX_SIZE = SIZE_MAX * PAGE_SIZE;
#endif

    // members
    uint64_t size_ = 0;
    uint32_t pmm_alloc_flags_ = PMM_ALLOC_FLAG_ANY;

    // a tree of pages
    VmPageList page_list_;
};

// VMO representing a physical range of memory
class VmObjectPhysical final : public VmObject {
public:
    static mxtl::RefPtr<VmObject> Create(paddr_t base, uint64_t size);

    virtual status_t Resize(uint64_t size) { return ERR_NOT_SUPPORTED; }

    virtual uint64_t size() const { return size_; }
    virtual size_t AllocatedPages() const { return 0; }

    virtual int64_t CommitRange(uint64_t offset, uint64_t len) { return ERR_NOT_SUPPORTED; }

    virtual int64_t CommitRangeContiguous(uint64_t offset, uint64_t len, uint8_t alignment_log2 = 0) { return ERR_NOT_SUPPORTED; }

    virtual vm_page_t* GetPage(uint64_t offset) { return nullptr; }

    virtual status_t GetPage(uint64_t offset, paddr_t* pa);

    virtual vm_page_t* FaultPage(uint64_t offset, uint pf_flags) { return nullptr; }

    virtual status_t FaultPage(uint64_t offset, uint pf_flags, paddr_t* pa);

    virtual status_t Read(void* ptr, uint64_t offset, size_t len, size_t* bytes_read) { return ERR_NOT_SUPPORTED; }
    virtual status_t Write(const void* ptr, uint64_t offset, size_t len, size_t* bytes_written) { return ERR_NOT_SUPPORTED; }

    virtual status_t ReadUser(user_ptr<void> ptr, uint64_t offset, size_t len, size_t* bytes_read) { return ERR_NOT_SUPPORTED; }
    virtual status_t WriteUser(user_ptr<const void> ptr, uint64_t offset, size_t len, size_t* bytes_written) { return ERR_NOT_SUPPORTED; }

    virtual status_t Lookup(uint64_t offset, uint64_t len, user_ptr<paddr_t>, size_t);

    virtual void Dump(bool page_dump = false);

private:
    // private constructor (use Create())
    VmObjectPhysical(paddr_t base, uint64_t size);

    // private destructor, only called from refptr
    virtual ~VmObjectPhysical();

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmObjectPhysical);

    // members
    uint64_t size_ = 0;
    paddr_t base_ = 0;
};
