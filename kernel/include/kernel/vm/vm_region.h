// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <kernel/vm/vm_page_list.h>
#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <stdint.h>

class VmAspace;
class VmObject;

class VmRegion : public mxtl::WAVLTreeContainable<mxtl::RefPtr<VmRegion>>, public mxtl::RefCounted<VmRegion> {
public:
    static mxtl::RefPtr<VmRegion> Create(VmAspace& aspace, vaddr_t base, size_t size,
                                         uint arch_mmu_flags, const char* name);
    ~VmRegion();

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmRegion);

    // accessors
    vaddr_t base() const { return base_; }
    size_t size() const { return size_; }
    uint arch_mmu_flags() const { return arch_mmu_flags_; }
    uint64_t object_offset() const { return object_offset_; }

    // set base address
    void set_base(vaddr_t vaddr) { base_ = vaddr; }

    void Dump() const;

    size_t AllocatedPages() const;

    // set the object that this region backs
    status_t SetObject(mxtl::RefPtr<VmObject> o, uint64_t offset);

    // map in pages from the underlying vm object, optionally committing pages as it goes
    status_t MapRange(size_t offset, size_t len, bool commit);

    // unmap all pages and remove dependency on vm object it has a ref to
    status_t Destroy();

    // unmap the region of memory in the container address space
    int Unmap();

    // change mapping permissions
    status_t Protect(uint arch_mmu_flags);

    // page fault in an address into the region
    status_t PageFault(vaddr_t va, uint pf_flags);

    mxtl::RefPtr<VmObject> vmo();

    // WAVL tree key function
    vaddr_t GetKey() const { return base(); }

private:
    // private constructor, use Create()
    VmRegion(VmAspace& aspace, vaddr_t base, size_t size, uint arch_mmu_flags, const char* name);

    // magic value
    static const uint32_t MAGIC = 0x564d5247; // VMRG
    uint32_t magic_ = MAGIC;

    // address/size within the container address space
    vaddr_t base_;
    size_t size_;

    // cached mapping flags (read/write/user/etc)
    uint arch_mmu_flags_;

    // pointer back to our member address space
    mxtl::RefPtr<VmAspace> aspace_;

    // pointer and region of the object we are mapping
    mxtl::RefPtr<VmObject> object_;
    uint64_t object_offset_ = 0;

    char name_[32];
};
