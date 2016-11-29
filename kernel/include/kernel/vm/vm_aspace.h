// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/mmu.h>
#include <assert.h>
#include <kernel/mutex.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_address_region.h>
#include <mxtl/deleter.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/macros.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>

class VmObject;

class VmAspace : public mxtl::DoublyLinkedListable<VmAspace*>, public mxtl::RefCounted<VmAspace> {
public:
    // complete initialization, may fail in OOM cases
    status_t Init();

    // factory that creates a user/kernel address space based on flags
    // may fail due to resource starvation
    static mxtl::RefPtr<VmAspace> Create(uint flags, const char* name);

    void Rename(const char* name);

    // flags
    static const uint32_t TYPE_USER = VMM_ASPACE_TYPE_USER;
    static const uint32_t TYPE_KERNEL = VMM_ASPACE_TYPE_KERNEL;
    // You probably do not want to use LOW_KERNEL.  It is primarily
    // used for SMP bootstrap to allow mappings of very low memory using
    // the standard VMM subsystem.
    static const uint32_t TYPE_LOW_KERNEL = VMM_ASPACE_TYPE_LOW_KERNEL;
    static const uint32_t TYPE_MASK = VMM_ASPACE_TYPE_MASK;

    // simple accessors
    vaddr_t base() const { return base_; }
    size_t size() const { return size_; }
    arch_aspace_t& arch_aspace() { return arch_aspace_; }
    bool is_user() const { return (flags_ & TYPE_MASK) == TYPE_USER; }
    mxtl::RefPtr<VmAddressRegion> root_vmar() { return root_vmar_; }

    // destroy but not free the address space
    status_t Destroy();

    // accessor for singleton kernel address space
    static VmAspace* kernel_aspace() { return kernel_aspace_; }

    // set the per thread aspace pointer to this
    void AttachToThread(thread_t* t);

    void Dump() const;

    size_t AllocatedPages() const;

    // Convenience method for traversing the tree of VMARs to find the deepest
    // VMAR in the tree that includes *va*.
    mxtl::RefPtr<VmAddressRegionOrMapping> FindRegion(vaddr_t va);

    // legacy functions to assist in the transition to VMARs
    // These all assume a flat VMAR structure in which all VMOs are mapped
    // as children of the root.  They will all assert if used on user aspaces
    // TODO(teisenbe): remove uses of these in favor of new VMAR interfaces
    status_t MapObject(mxtl::RefPtr<VmObject> vmo, const char* name, uint64_t offset, size_t size,
                       void** ptr, uint8_t align_pow2, size_t min_alloc_gap, uint vmm_flags,
                       uint arch_mmu_flags);
    status_t ReserveSpace(const char* name, size_t size, vaddr_t vaddr);
    status_t AllocPhysical(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                           size_t min_alloc_gap, paddr_t paddr, uint vmm_flags,
                           uint arch_mmu_flags);
    status_t AllocContiguous(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                             size_t min_alloc_gap, uint vmm_flags, uint arch_mmu_flags);
    status_t Alloc(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                   size_t min_alloc_gap, uint vmm_flags, uint arch_mmu_flags);
    status_t FreeRegion(vaddr_t va);

protected:
    // Share the aspace lock with VmAddressRegion/VmMapping so they can serialize
    // changes to the aspace.
    friend class VmAddressRegionOrMapping;
    friend class VmAddressRegion;
    friend class VmMapping;
    mutex_t& lock() { return lock_; }

private:
    // can only be constructed via factory
    VmAspace(vaddr_t base, size_t size, uint32_t flags, const char* name);

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmAspace);

    // private destructor that can only be used from the ref ptr or vmm_free_aspace
    ~VmAspace();
    friend mxtl::default_delete<VmAspace>;
    friend status_t vmm_free_aspace(vmm_aspace_t* _aspace);

    // internal page fault routine, friended to be only called by vmm_page_fault_handler
    status_t PageFault(vaddr_t va, uint flags);
    friend status_t vmm_page_fault_handler(vaddr_t va, uint flags);

    // magic
    static const uint32_t MAGIC = 0x564d4153; // VMAS
    uint32_t magic_ = MAGIC;

    // members
    vaddr_t base_;
    size_t size_;
    uint32_t flags_;
    char name_[32];
    bool aspace_destroyed_ = false;

    mutable mutex_t lock_ = MUTEX_INITIAL_VALUE(lock_);

    // root of virtual address space
    // TODO(teisenbe): maybe embed this
    mxtl::RefPtr<VmAddressRegion> root_vmar_;

    // architecturally specific part of the aspace
    arch_aspace_t arch_aspace_ = {};

    // initialization routines need to construct the singleton kernel address space
    // at a particular points in the bootup process
    static void KernelAspaceInitPreHeap();
    static VmAspace* kernel_aspace_;
    friend void vm_init_preheap(uint level);
};

void DumpAllAspaces();

// hack to convert from vmm_aspace_t to VmAspace
static VmAspace* vmm_aspace_to_obj(vmm_aspace_t* aspace) {
    return reinterpret_cast<VmAspace*>(aspace);
}

static const VmAspace* vmm_aspace_to_obj(const vmm_aspace_t* aspace) {
    return reinterpret_cast<const VmAspace*>(aspace);
}
