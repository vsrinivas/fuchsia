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
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <stdint.h>

// The base vm object that holds a range of bytes of data
//
// Can be created without mapping and used as a container of data, or mappable
// into an address space via VmAspace::MapObject

class VmObject : public mxtl::RefCounted<VmObject> {
public:
    static mxtl::RefPtr<VmObject> Create(uint32_t pmm_alloc_flags, uint64_t size);

    static mxtl::RefPtr<VmObject> CreateFromROData(const void* data,
                                                   size_t size);

    status_t Resize(uint64_t size);

    uint64_t size() const { return size_; }

    // add a page to the object
    status_t AddPage(vm_page_t* p, uint64_t offset);

    // find physical pages to back the range of the object
    int64_t CommitRange(uint64_t offset, uint64_t len);

    // find a contiguous run of physical pages to back the range of the object
    int64_t CommitRangeContiguous(uint64_t offset, uint64_t len, uint8_t alignment_log2 = 0);

    // get a pointer to a page at a given offset
    vm_page_t* GetPage(uint64_t offset);

    // fault in a page at a given offset with PF_FLAGS
    vm_page_t* FaultPage(uint64_t offset, uint pf_flags);

    // read/write operators against kernel pointers only
    status_t Read(void* ptr, uint64_t offset, size_t len, size_t* bytes_read);
    status_t Write(const void* ptr, uint64_t offset, size_t len, size_t* bytes_written);

    // read/write operators against user space pointers only
    status_t ReadUser(user_ptr<void> ptr, uint64_t offset, size_t len, size_t* bytes_read);
    status_t WriteUser(user_ptr<const void> ptr, uint64_t offset, size_t len, size_t* bytes_written);

    // translate a range of the vmo to physical addresses and store in the buffer
    status_t Lookup(uint64_t offset, uint64_t len, user_ptr<paddr_t>, size_t);

    void Dump();

private:
    // kill copy constructors
    VmObject(const VmObject& o) = delete;
    VmObject& operator=(VmObject& o) = delete;

    // private constructor (use Create())
    explicit VmObject(uint32_t pmm_alloc_flags);

    // private destructor, only called from refptr
    ~VmObject();
    friend mxtl::RefPtr<VmObject>;

    // unlocked versions of the above routines
    vm_page_t* FaultPageLocked(uint64_t offset, uint pf_flags);
    vm_page_t* GetPageLocked(uint64_t offset);

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

    // magic value
    static const uint32_t MAGIC = 0x564d4f5f; // VMO_
    uint32_t magic_ = MAGIC;

    // members
    uint64_t size_ = 0;
    uint32_t pmm_alloc_flags_ = PMM_ALLOC_FLAG_ANY;
    Mutex lock_;

    // a tree of pages
    VmPageList page_list_;
};
