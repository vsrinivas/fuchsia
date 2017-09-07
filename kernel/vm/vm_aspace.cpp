// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <vm/vm_aspace.h>

#include "vm_priv.h"
#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/cmdline.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <lib/crypto/global_prng.h>
#include <lib/crypto/prng.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/type_support.h>
#include <safeint/safe_math.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <vm/vm_address_region.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>
#include <vm/vm_object_physical.h>

#if WITH_LIB_VDSO
#include <lib/vdso.h>
#endif

using fbl::AutoLock;

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

// pointer to a singleton kernel address space
VmAspace* VmAspace::kernel_aspace_ = nullptr;

// pointer to the dummy root VMAR singleton
static VmAddressRegion* dummy_root_vmar = nullptr;

// list of all address spaces
static mutex_t aspace_list_lock = MUTEX_INITIAL_VALUE(aspace_list_lock);
static fbl::DoublyLinkedList<VmAspace*> aspaces;

// called once at boot to initialize the singleton kernel address space
void VmAspace::KernelAspaceInitPreHeap() {
    // the singleton kernel address space
    static VmAspace _kernel_aspace(KERNEL_ASPACE_BASE, KERNEL_ASPACE_SIZE, VmAspace::TYPE_KERNEL, "kernel");

    // the singleton dummy root vmar (used to break a reference cycle in
    // Destroy())
    static VmAddressRegionDummy dummy_vmar;
#if LK_DEBUGLEVEL > 1
    _kernel_aspace.Adopt();
    dummy_vmar.Adopt();
#endif

    dummy_root_vmar = &dummy_vmar;

    static VmAddressRegion _kernel_root_vmar(_kernel_aspace);

    _kernel_aspace.root_vmar_ = fbl::AdoptRef(&_kernel_root_vmar);

    auto err = _kernel_aspace.Init();
    ASSERT(err >= 0);

    // save a pointer to the singleton kernel address space
    VmAspace::kernel_aspace_ = &_kernel_aspace;
    aspaces.push_front(kernel_aspace_);
}

// simple test routines
static inline bool is_inside(VmAspace& aspace, vaddr_t vaddr) {
    return (vaddr >= aspace.base() && vaddr <= aspace.base() + aspace.size() - 1);
}

static inline bool is_inside(VmAspace& aspace, VmAddressRegion& r) {
    // is the starting address within the address space
    if (!is_inside(aspace, r.base()))
        return false;

    if (r.size() == 0)
        return true;

    // see if the size is enough to wrap the integer
    if (r.base() + r.size() - 1 < r.base())
        return false;

    // test to see if the end address is within the address space's
    if (r.base() + r.size() - 1 > aspace.base() + aspace.size() - 1)
        return false;

    return true;
}

static inline size_t trim_to_aspace(VmAspace& aspace, vaddr_t vaddr, size_t size) {
    DEBUG_ASSERT(is_inside(aspace, vaddr));

    if (size == 0)
        return size;

    size_t offset = vaddr - aspace.base();

    // LTRACEF("vaddr 0x%lx size 0x%zx offset 0x%zx aspace base 0x%lx aspace size 0x%zx\n",
    //        vaddr, size, offset, aspace.base(), aspace.size());

    if (offset + size < offset)
        size = ULONG_MAX - offset - 1;

    // LTRACEF("size now 0x%zx\n", size);

    if (offset + size >= aspace.size() - 1)
        size = aspace.size() - offset;

    // LTRACEF("size now 0x%zx\n", size);

    return size;
}

VmAspace::VmAspace(vaddr_t base, size_t size, uint32_t flags, const char* name)
    : base_(base), size_(size), flags_(flags), root_vmar_(nullptr), aslr_prng_(nullptr, 0) {

    DEBUG_ASSERT(size != 0);
    DEBUG_ASSERT(base + size - 1 >= base);

    Rename(name);

    LTRACEF("%p '%s'\n", this, name_);
}

status_t VmAspace::Init() {
    canary_.Assert();

    LTRACEF("%p '%s'\n", this, name_);

    // initialize the architecturally specific part
    bool is_high_kernel = (flags_ & TYPE_MASK) == TYPE_KERNEL;
    bool is_guest = (flags_ & TYPE_MASK) == TYPE_GUEST_PHYS;
    uint arch_aspace_flags =
        (is_high_kernel ? ARCH_ASPACE_FLAG_KERNEL : 0u) |
        (is_guest ? ARCH_ASPACE_FLAG_GUEST_PASPACE : 0u);
    status_t status = arch_aspace_.Init(base_, size_, arch_aspace_flags);
    if (status != MX_OK) {
        return status;
    }

    InitializeAslr();

    if (likely(!root_vmar_)) {
        return VmAddressRegion::CreateRoot(*this, VMAR_FLAG_CAN_MAP_SPECIFIC, &root_vmar_);
    }
    return MX_OK;
}

fbl::RefPtr<VmAspace> VmAspace::Create(uint32_t flags, const char* name) {
    LTRACEF("flags 0x%x, name '%s'\n", flags, name);

    vaddr_t base;
    size_t size;
    switch (flags & TYPE_MASK) {
    case TYPE_USER:
        base = USER_ASPACE_BASE;
        size = USER_ASPACE_SIZE;
        break;
    case TYPE_KERNEL:
        base = KERNEL_ASPACE_BASE;
        size = KERNEL_ASPACE_SIZE;
        break;
    case TYPE_LOW_KERNEL:
        base = 0;
        size = USER_ASPACE_BASE + USER_ASPACE_SIZE;
        break;
    case TYPE_GUEST_PHYS:
        base = GUEST_PHYSICAL_ASPACE_BASE;
        size = GUEST_PHYSICAL_ASPACE_SIZE;
        break;
    default:
        panic("Invalid aspace type");
    }

    fbl::AllocChecker ac;
    auto aspace = fbl::AdoptRef(new (&ac) VmAspace(base, size, flags, name));
    if (!ac.check())
        return nullptr;

    // initialize the arch specific component to our address space
    auto err = aspace->Init();
    if (err < 0) {
        aspace->Destroy();
        return nullptr;
    }

    // add it to the global list
    {
        AutoLock a(&aspace_list_lock);
        aspaces.push_back(aspace.get());
    }

    // return a ref pointer to the aspace
    return fbl::move(aspace);
}

void VmAspace::Rename(const char* name) {
    canary_.Assert();
    strlcpy(name_, name ? name : "unnamed", sizeof(name_));
}

VmAspace::~VmAspace() {
    canary_.Assert();
    LTRACEF("%p '%s'\n", this, name_);

    // we have to have already been destroyed before freeing
    DEBUG_ASSERT(aspace_destroyed_);

    // pop it out of the global aspace list
    {
        AutoLock a(&aspace_list_lock);
        if (this->InContainer()) {
            aspaces.erase(*this);
        }
    }

    // destroy the arch portion of the aspace
    // TODO(teisenbe): Move this to Destroy().  Currently can't move since
    // ProcessDispatcher calls Destroy() from the context of a thread in the
    // aspace.
    arch_aspace_.Destroy();
}

fbl::RefPtr<VmAddressRegion> VmAspace::RootVmar() {
    AutoLock guard(&lock_);
    fbl::RefPtr<VmAddressRegion> ref(root_vmar_);
    return fbl::move(ref);
}

status_t VmAspace::Destroy() {
    canary_.Assert();
    LTRACEF("%p '%s'\n", this, name_);

    AutoLock guard(&lock_);

#if WITH_LIB_VDSO
    // Don't let a vDSO mapping prevent destroying a VMAR
    // when the whole process is being destroyed.
    vdso_code_mapping_.reset();
#endif

    // tear down and free all of the regions in our address space
    if (root_vmar_) {
        status_t status = root_vmar_->DestroyLocked();
        if (status != MX_OK && status != MX_ERR_BAD_STATE) {
            return status;
        }
    }
    aspace_destroyed_ = true;

    // Break the reference cycle between this aspace and the root VMAR
    root_vmar_.reset(dummy_root_vmar);

    return MX_OK;
}

bool VmAspace::is_destroyed() const {
    AutoLock guard(&lock_);
    return aspace_destroyed_;
}

status_t VmAspace::MapObjectInternal(fbl::RefPtr<VmObject> vmo, const char* name, uint64_t offset,
                                     size_t size, void** ptr, uint8_t align_pow2, uint vmm_flags,
                                     uint arch_mmu_flags) {

    canary_.Assert();
    LTRACEF("aspace %p name '%s' vmo %p, offset %#" PRIx64 " size %#zx "
            "ptr %p align %hhu vmm_flags %#x arch_mmu_flags %#x\n",
            this, name, vmo.get(), offset, size, ptr ? *ptr : 0, align_pow2, vmm_flags, arch_mmu_flags);

    DEBUG_ASSERT(!is_user() || !(arch_mmu_flags & ARCH_MMU_FLAG_PERM_USER));

    size = ROUNDUP(size, PAGE_SIZE);
    if (size == 0)
        return MX_ERR_INVALID_ARGS;
    if (!vmo)
        return MX_ERR_INVALID_ARGS;
    if (!IS_PAGE_ALIGNED(offset))
        return MX_ERR_INVALID_ARGS;

    vaddr_t vmar_offset = 0;
    // if they're asking for a specific spot or starting address, copy the address
    if (vmm_flags & VMM_FLAG_VALLOC_SPECIFIC) {
        // can't ask for a specific spot and then not provide one
        if (!ptr) {
            return MX_ERR_INVALID_ARGS;
        }
        vmar_offset = reinterpret_cast<vaddr_t>(*ptr);

        // check that it's page aligned
        if (!IS_PAGE_ALIGNED(vmar_offset) || vmar_offset < base_)
            return MX_ERR_INVALID_ARGS;

        vmar_offset -= base_;
    }

    uint32_t vmar_flags = 0;
    if (vmm_flags & VMM_FLAG_VALLOC_SPECIFIC) {
        vmar_flags |= VMAR_FLAG_SPECIFIC;
    }

    // Create the mappings with all of the CAN_* RWX flags, so that
    // Protect() can transition them arbitrarily.  This is not desirable for the
    // long-term.
    vmar_flags |= VMAR_CAN_RWX_FLAGS;

    // allocate a region and put it in the aspace list
    fbl::RefPtr<VmMapping> r(nullptr);
    status_t status = RootVmar()->CreateVmMapping(vmar_offset, size, align_pow2,
                                                  vmar_flags,
                                                  vmo, offset, arch_mmu_flags, name, &r);
    if (status != MX_OK) {
        return status;
    }

    // if we're committing it, map the region now
    if (vmm_flags & VMM_FLAG_COMMIT) {
        auto err = r->MapRange(0, size, true);
        if (err < 0)
            return err;
    }

    // return the vaddr if requested
    if (ptr)
        *ptr = (void*)r->base();

    return MX_OK;
}

status_t VmAspace::ReserveSpace(const char* name, size_t size, vaddr_t vaddr) {
    canary_.Assert();
    LTRACEF("aspace %p name '%s' size %#zx vaddr %#" PRIxPTR "\n", this, name, size, vaddr);

    DEBUG_ASSERT(IS_PAGE_ALIGNED(vaddr));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(size));

    size = ROUNDUP_PAGE_SIZE(size);
    if (size == 0)
        return MX_OK;
    if (!IS_PAGE_ALIGNED(vaddr))
        return MX_ERR_INVALID_ARGS;
    if (!is_inside(*this, vaddr))
        return MX_ERR_OUT_OF_RANGE;

    // trim the size
    size = trim_to_aspace(*this, vaddr, size);

    // allocate a zero length vm object to back it
    // TODO: decide if a null vmo object is worth it
    fbl::RefPtr<VmObject> vmo;
    mx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0, &vmo);
    if (status != MX_OK)
        return status;

    // lookup how it's already mapped
    uint arch_mmu_flags = 0;
    auto err = arch_aspace_.Query(vaddr, nullptr, &arch_mmu_flags);
    if (err) {
        // if it wasn't already mapped, use some sort of strict default
        arch_mmu_flags = ARCH_MMU_FLAG_CACHED | ARCH_MMU_FLAG_PERM_READ;
    }

    // map it, creating a new region
    void* ptr = reinterpret_cast<void*>(vaddr);
    return MapObjectInternal(fbl::move(vmo), name, 0, size, &ptr, 0, VMM_FLAG_VALLOC_SPECIFIC,
                             arch_mmu_flags);
}

status_t VmAspace::AllocPhysical(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                                 paddr_t paddr, uint vmm_flags, uint arch_mmu_flags) {
    canary_.Assert();
    LTRACEF("aspace %p name '%s' size %#zx ptr %p paddr %#" PRIxPTR " vmm_flags 0x%x arch_mmu_flags 0x%x\n",
            this, name, size, ptr ? *ptr : 0, paddr, vmm_flags, arch_mmu_flags);

    DEBUG_ASSERT(IS_PAGE_ALIGNED(paddr));

    if (size == 0)
        return MX_OK;
    if (!IS_PAGE_ALIGNED(paddr))
        return MX_ERR_INVALID_ARGS;

    size = ROUNDUP_PAGE_SIZE(size);

    // create a vm object to back it
    fbl::RefPtr<VmObject> vmo;
    status_t status = VmObjectPhysical::Create(paddr, size, &vmo);
    if (status != MX_OK)
        return status;

    // force it to be mapped up front
    // TODO: add new flag to precisely mean pre-map
    vmm_flags |= VMM_FLAG_COMMIT;

    // Apply the cache policy
    if (vmo->SetMappingCachePolicy(arch_mmu_flags & ARCH_MMU_FLAG_CACHE_MASK) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    arch_mmu_flags &= ~ARCH_MMU_FLAG_CACHE_MASK;
    return MapObjectInternal(fbl::move(vmo), name, 0, size, ptr, align_pow2, vmm_flags,
                             arch_mmu_flags);
}

status_t VmAspace::AllocContiguous(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                                   uint vmm_flags, uint arch_mmu_flags) {
    canary_.Assert();
    LTRACEF("aspace %p name '%s' size 0x%zx ptr %p align %hhu vmm_flags 0x%x arch_mmu_flags 0x%x\n", this,
            name, size, ptr ? *ptr : 0, align_pow2, vmm_flags, arch_mmu_flags);

    size = ROUNDUP(size, PAGE_SIZE);
    if (size == 0)
        return MX_ERR_INVALID_ARGS;

    // test for invalid flags
    if (!(vmm_flags & VMM_FLAG_COMMIT))
        return MX_ERR_INVALID_ARGS;

    // create a vm object to back it
    fbl::RefPtr<VmObject> vmo;
    mx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, size, &vmo);
    if (status != MX_OK)
        return status;

    // always immediately commit memory to the object
    uint64_t committed;
    status = vmo->CommitRangeContiguous(0, size, &committed, align_pow2);
    if (status < 0)
        return status;
    if (static_cast<size_t>(committed) < size) {
        LTRACEF("failed to allocate enough pages (asked for %zu, got %zu)\n", size / PAGE_SIZE,
                static_cast<size_t>(committed) / PAGE_SIZE);
        return MX_ERR_NO_MEMORY;
    }

    return MapObjectInternal(fbl::move(vmo), name, 0, size, ptr, align_pow2, vmm_flags,
                             arch_mmu_flags);
}

status_t VmAspace::Alloc(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                         uint vmm_flags, uint arch_mmu_flags) {
    canary_.Assert();
    LTRACEF("aspace %p name '%s' size 0x%zx ptr %p align %hhu vmm_flags 0x%x arch_mmu_flags 0x%x\n", this,
            name, size, ptr ? *ptr : 0, align_pow2, vmm_flags, arch_mmu_flags);

    size = ROUNDUP(size, PAGE_SIZE);
    if (size == 0)
        return MX_ERR_INVALID_ARGS;

    // allocate a vm object to back it
    fbl::RefPtr<VmObject> vmo;
    mx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, size, &vmo);
    if (status != MX_OK)
        return status;

    // commit memory up front if requested
    if (vmm_flags & VMM_FLAG_COMMIT) {
        // commit memory to the object
        uint64_t committed;
        status = vmo->CommitRange(0, size, &committed);
        if (status < 0)
            return status;
        if (static_cast<size_t>(committed) < size) {
            LTRACEF("failed to allocate enough pages (asked for %zu, got %zu)\n", size / PAGE_SIZE,
                    static_cast<size_t>(committed) / PAGE_SIZE);
            return MX_ERR_NO_MEMORY;
        }
    }

    // map it, creating a new region
    return MapObjectInternal(fbl::move(vmo), name, 0, size, ptr, align_pow2, vmm_flags,
                             arch_mmu_flags);
}

status_t VmAspace::FreeRegion(vaddr_t va) {
    DEBUG_ASSERT(!is_user());

    fbl::RefPtr<VmAddressRegionOrMapping> r = RootVmar()->FindRegion(va);
    if (!r) {
        return MX_ERR_NOT_FOUND;
    }

    return r->Destroy();
}

fbl::RefPtr<VmAddressRegionOrMapping> VmAspace::FindRegion(vaddr_t va) {
    fbl::RefPtr<VmAddressRegion> vmar(RootVmar());
    while (1) {
        fbl::RefPtr<VmAddressRegionOrMapping> next(vmar->FindRegion(va));
        if (!next) {
            return vmar;
        }

        if (next->is_mapping()) {
            return next;
        }

        vmar = next->as_vm_address_region();
    }
}

void VmAspace::AttachToThread(thread_t* t) {
    canary_.Assert();
    DEBUG_ASSERT(t);

    // point the lk thread at our object via the dummy C vmm_aspace_t struct
    AutoThreadLock lock;

    // not prepared to handle setting a new address space or one on a running thread
    DEBUG_ASSERT(!t->aspace);
    DEBUG_ASSERT(t->state != THREAD_RUNNING);

    t->aspace = reinterpret_cast<vmm_aspace_t*>(this);
}

status_t VmAspace::PageFault(vaddr_t va, uint flags) {
    canary_.Assert();
    DEBUG_ASSERT(!aspace_destroyed_);
    LTRACEF("va %#" PRIxPTR ", flags %#x\n", va, flags);

    // for now, hold the aspace lock across the page fault operation,
    // which stops any other operations on the address space from moving
    // the region out from underneath it
    AutoLock a(&lock_);

    return root_vmar_->PageFault(va, flags);
}

void VmAspace::Dump(bool verbose) const {
    canary_.Assert();
    printf("as %p [%#" PRIxPTR " %#" PRIxPTR "] sz %#zx fl %#x ref %d '%s'\n", this,
           base_, base_ + size_ - 1, size_, flags_, ref_count_debug(), name_);

    AutoLock a(&lock_);

    if (verbose)
        root_vmar_->Dump(1, verbose);
}

bool VmAspace::EnumerateChildren(VmEnumerator* ve) {
    canary_.Assert();
    DEBUG_ASSERT(ve != nullptr);
    AutoLock a(&lock_);
    if (root_vmar_ == nullptr || aspace_destroyed_) {
        // Aspace hasn't been initialized or has already been destroyed.
        return true;
    }
    DEBUG_ASSERT(root_vmar_->IsAliveLocked());
    if (!ve->OnVmAddressRegion(root_vmar_.get(), 0)) {
        return false;
    }
    return root_vmar_->EnumerateChildrenLocked(ve, 1);
}

void DumpAllAspaces(bool verbose) {
    AutoLock a(&aspace_list_lock);

    for (const auto& a : aspaces)
        a.Dump(verbose);
}

VmAspace* VmAspace::vaddr_to_aspace(uintptr_t address) {
    if (is_kernel_address(address)) {
        return kernel_aspace();
    } else if (is_user_address(address)) {
        return vmm_aspace_to_obj(get_current_thread()->aspace);
    } else {
        return nullptr;
    }
}

// TODO(dbort): Use GetMemoryUsage()
size_t VmAspace::AllocatedPages() const {
    canary_.Assert();

    AutoLock a(&lock_);
    return root_vmar_->AllocatedPagesLocked();
}

void VmAspace::InitializeAslr() {
    aslr_enabled_ = is_user() && !cmdline_get_bool("aslr.disable", false);

    crypto::GlobalPRNG::GetInstance()->Draw(aslr_seed_, sizeof(aslr_seed_));
    aslr_prng_.AddEntropy(aslr_seed_, sizeof(aslr_seed_));
}

#if WITH_LIB_VDSO
uintptr_t VmAspace::vdso_base_address() const {
    AutoLock a(&lock_);
    return VDso::base_address(vdso_code_mapping_);
}

uintptr_t VmAspace::vdso_code_address() const {
    AutoLock a(&lock_);
    return vdso_code_mapping_ ? vdso_code_mapping_->base() : 0;
}
#endif
