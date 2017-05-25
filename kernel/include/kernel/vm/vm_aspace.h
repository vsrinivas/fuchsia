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
#include <lib/crypto/prng.h>
#include <mxtl/canary.h>
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
    static const uint32_t TYPE_USER       = (0 << 0);
    static const uint32_t TYPE_KERNEL     = (1 << 0);
    // You probably do not want to use LOW_KERNEL.  It is primarily
    // used for SMP bootstrap to allow mappings of very low memory using
    // the standard VMM subsystem.
    static const uint32_t TYPE_LOW_KERNEL = (2 << 0);
    static const uint32_t TYPE_MASK       = (3 << 0);

    // simple accessors
    vaddr_t base() const { return base_; }
    size_t size() const { return size_; }
    const char* name() const { return name_; }
    arch_aspace_t& arch_aspace() { return arch_aspace_; }
    bool is_user() const { return (flags_ & TYPE_MASK) == TYPE_USER; }
    bool is_aslr_enabled() const { return aslr_enabled_; }

    // Get the root VMAR (briefly acquires the aspace lock)
    mxtl::RefPtr<VmAddressRegion> RootVmar();

    // destroy but not free the address space
    status_t Destroy();

    // Returns true if the address space has been destroyed.
    bool is_destroyed() const;

    // accessor for singleton kernel address space
    static VmAspace* kernel_aspace() { return kernel_aspace_; }

    // set the per thread aspace pointer to this
    void AttachToThread(thread_t* t);

    void Dump(bool verbose) const;

    // Traverses the VM tree rooted at this node, in depth-first pre-order. If
    // any methods of |ve| return false, the traversal stops and this method
    // returns false. Returns true otherwise.
    bool EnumerateChildren(VmEnumerator* ve);

    // A collection of memory usage counts.
    struct vm_usage_t {
        // A count of pages covered by VmMapping ranges.
        size_t mapped_pages;

        // For the fields below, a page is considered committed if a VmMapping
        // covers a range of a VmObject that contains that page, and that page
        // has physical memory allocated to it.

        // A count of committed pages that are only mapped into this address
        // space.
        size_t private_pages;

        // A count of committed pages that are mapped into this and at least
        // one other address spaces.
        size_t shared_pages;

        // A number that estimates the fraction of shared_pages that this
        // address space is responsible for keeping alive.
        //
        // An estimate of:
        //   For each shared, committed page:
        //   scaled_shared_bytes +=
        //       PAGE_SIZE / (number of address spaces mapping this page)
        //
        // This number is strictly smaller than shared_pages * PAGE_SIZE.
        size_t scaled_shared_bytes;
    };

    // Counts memory usage under the VmAspace.
    status_t GetMemoryUsage(vm_usage_t* usage);

    size_t AllocatedPages() const;

    // Convenience method for traversing the tree of VMARs to find the deepest
    // VMAR in the tree that includes *va*.
    mxtl::RefPtr<VmAddressRegionOrMapping> FindRegion(vaddr_t va);

    // legacy functions to assist in the transition to VMARs
    // These all assume a flat VMAR structure in which all VMOs are mapped
    // as children of the root.  They will all assert if used on user aspaces
    // TODO(teisenbe): remove uses of these in favor of new VMAR interfaces
    status_t ReserveSpace(const char* name, size_t size, vaddr_t vaddr);
    status_t AllocPhysical(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                           paddr_t paddr, uint vmm_flags,
                           uint arch_mmu_flags);
    status_t AllocContiguous(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                             uint vmm_flags, uint arch_mmu_flags);
    status_t Alloc(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                   uint vmm_flags, uint arch_mmu_flags);
    status_t FreeRegion(vaddr_t va);

    // Internal use function for mapping VMOs.  Do not use.  This is exposed in
    // the public API purely for tests.
    status_t MapObjectInternal(mxtl::RefPtr<VmObject> vmo, const char* name, uint64_t offset,
                               size_t size, void** ptr, uint8_t align_pow2, uint vmm_flags,
                               uint arch_mmu_flags);

#if WITH_LIB_VDSO
    uintptr_t vdso_base_address() const;
    uintptr_t vdso_code_address() const;
#endif

protected:
    // Share the aspace lock with VmAddressRegion/VmMapping so they can serialize
    // changes to the aspace.
    friend class VmAddressRegionOrMapping;
    friend class VmAddressRegion;
    friend class VmMapping;
    mutex_t* lock() { return &lock_; }

    // Expose the PRNG for ASLR to VmAddressRegion
    crypto::PRNG& AslrPrng() {
        DEBUG_ASSERT(aslr_enabled_);
        return aslr_prng_;
    }

private:
    // can only be constructed via factory
    VmAspace(vaddr_t base, size_t size, uint32_t flags, const char* name);

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmAspace);

    // private destructor that can only be used from the ref ptr
    ~VmAspace();
    friend mxtl::RefPtr<VmAspace>;

    // internal page fault routine, friended to be only called by vmm_page_fault_handler
    status_t PageFault(vaddr_t va, uint flags);
    friend status_t vmm_page_fault_handler(vaddr_t va, uint flags);

    void InitializeAslr();

    // magic
    mxtl::Canary<mxtl::magic("VMAS")> canary_;

    // members
    vaddr_t base_;
    size_t size_;
    uint32_t flags_;
    char name_[32];
    bool aspace_destroyed_ = false;
    bool aslr_enabled_ = false;

    mutable mutex_t lock_ = MUTEX_INITIAL_VALUE(lock_);

    // root of virtual address space
    // Access to this reference is guarded by lock_.
    mxtl::RefPtr<VmAddressRegion> root_vmar_;

    // PRNG used by VMARs for address choices.  We record the seed to enable
    // reproducible debugging.
    crypto::PRNG aslr_prng_;
    uint8_t aslr_seed_[crypto::PRNG::kMinEntropy];

    // architecturally specific part of the aspace
    arch_aspace_t arch_aspace_ = {};

#if WITH_LIB_VDSO
    mxtl::RefPtr<VmMapping> vdso_code_mapping_;
#endif

    // initialization routines need to construct the singleton kernel address space
    // at a particular points in the bootup process
    static void KernelAspaceInitPreHeap();
    static VmAspace* kernel_aspace_;
    friend void vm_init_preheap(uint level);
};

void DumpAllAspaces(bool verbose);

// hack to convert from vmm_aspace_t to VmAspace
static VmAspace* vmm_aspace_to_obj(vmm_aspace_t* aspace) {
    return reinterpret_cast<VmAspace*>(aspace);
}

static const VmAspace* vmm_aspace_to_obj(const vmm_aspace_t* aspace) {
    return reinterpret_cast<const VmAspace*>(aspace);
}
