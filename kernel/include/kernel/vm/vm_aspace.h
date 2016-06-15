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
#include <utils/intrusive_double_list.h>
#include <utils/ref_counted.h>
#include <utils/ref_ptr.h>

class VmRegion;
class VmObject;

class VmAspace : public utils::RefCounted<VmAspace> {
public:
    // complete initialization, may fail in OOM cases
    status_t Init();

    // factory that creates a user/kernel address space based on flags
    // may fail due to resource starvation
    static utils::RefPtr<VmAspace> Create(uint flags, const char* name);

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

    // global aspace list
    void list_set_prev(VmAspace* node) { prev_ = node; }
    void list_set_next(VmAspace* node) { next_ = node; }
    VmAspace* list_prev() { return prev_; }
    VmAspace* list_next() { return next_; }

    // map a vm object at a given offset
    status_t MapObject(utils::RefPtr<VmObject> vmo, const char* name, uint64_t offset, size_t size,
                       void** ptr, uint8_t align_pow2, uint vmm_flags, uint arch_mmu_flags);

    // common routines, mostly used by internal kernel code

    // create a blank map of vm address space
    status_t ReserveSpace(const char* name, size_t size, vaddr_t vaddr);

    // allocate a vm region mapping a physical range of memory
    status_t AllocPhysical(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                           paddr_t paddr, uint vmm_flags, uint arch_mmu_flags);

    // allocate a block of virtual memory
    status_t Alloc(const char* name, size_t size, void** ptr, uint8_t align_pow2, uint vmm_flags,
                   uint arch_mmu_flags);

    // allocate a block of virtual memory with physically contiguous backing pages
    status_t AllocContiguous(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                             uint vmm_flags, uint arch_mmu_flags);

    // return a pointer to a region based on virtual address
    utils::RefPtr<VmRegion> FindRegion(vaddr_t vaddr);

    // free the region at a given address
    status_t FreeRegion(vaddr_t vaddr);

    // destroy but not free the address space
    status_t Destroy();

    // accessor for singleton kernel address space
    static VmAspace* kernel_aspace() {
        return kernel_aspace_;
    }

    // set the per thread aspace pointer to this
    void AttachToThread(thread_t* t);

    void Dump();

private:
    // nocopy
    VmAspace(const VmAspace&) = delete;
    VmAspace& operator=(const VmAspace&) = delete;

    // can only be constructed via factory
    VmAspace(vaddr_t base, size_t size, uint32_t flags, const char* name);

    // private destructor that can only be used from the ref ptr or vmm_free_aspace
    ~VmAspace();
    friend utils::RefPtr<VmAspace>;
    friend status_t vmm_free_aspace(vmm_aspace_t* _aspace);

    // internal page fault routine, friended to be only called by vmm_page_fault_handler
    status_t PageFault(vaddr_t va, uint flags);
    friend status_t vmm_page_fault_handler(vaddr_t va, uint flags);

    // private internal routines
    status_t AddRegion(utils::RefPtr<VmRegion> r);
    utils::RefPtr<VmRegion> AllocRegion(const char* name, size_t size, vaddr_t vaddr,
                                        uint8_t align_pow2, uint32_t vmm_flags,
                                        uint arch_mmu_flags);
    vaddr_t AllocSpot(size_t size, uint8_t align_pow2, uint arch_mmu_flags, VmRegion** before);
    VmRegion* FindRegionLocked(vaddr_t vaddr);

    // magic
    static const uint32_t MAGIC = 0x564d4153; // VMAS
    uint32_t magic_ = MAGIC;

    // members
    vaddr_t base_;
    size_t size_;
    uint32_t flags_;
    char name_[32];

    mutex_t lock_ = MUTEX_INITIAL_VALUE(lock_);

    // pointers for the global list of aspaces
    VmAspace* prev_ = nullptr;
    VmAspace* next_ = nullptr;

    // sorted list of regions
    utils::DoublyLinkedList<VmRegion> regions_;

    // architecturally specific part of the aspace
    arch_aspace_t arch_aspace_ = {};

    // initialization routines need to construct the singleton kernel address space
    // at a particular points in the bootup process
    static void KernelAspaceInit();
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
