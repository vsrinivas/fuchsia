// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/vm/vm_aspace.h>

#include "vm_priv.h"
#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/auto_lock.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_object.h>
#include <kernel/vm/vm_region.h>
#include <lk/init.h>
#include <mxtl/auto_call.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/type_support.h>
#include <new.h>
#include <safeint/safe_math.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

// Maximum size of allocation gap that can be requested
#define MAX_MIN_ALLOC_GAP (PAGE_SIZE * 1024)

// pointer to a singleton kernel address space
VmAspace* VmAspace::kernel_aspace_ = nullptr;

// list of all address spaces
static mutex_t aspace_list_lock = MUTEX_INITIAL_VALUE(aspace_list_lock);
static mxtl::DoublyLinkedList<VmAspace*> aspaces;

// called once at boot to initialize the singleton kernel address space
void VmAspace::KernelAspaceInitPreHeap() {
    // the singleton kernel address space
    static VmAspace _kernel_aspace(KERNEL_ASPACE_BASE, KERNEL_ASPACE_SIZE, VmAspace::TYPE_KERNEL,
                                   "kernel");
    auto err = _kernel_aspace.Init();
    ASSERT(err >= 0);

#if LK_DEBUGLEVEL > 0
    _kernel_aspace.Adopt();
#endif

    // save a pointer to the singleton kernel address space
    VmAspace::kernel_aspace_ = &_kernel_aspace;
    aspaces.push_front(kernel_aspace_);
}

// simple test routines
static inline bool is_inside(VmAspace& aspace, vaddr_t vaddr) {
    return (vaddr >= aspace.base() && vaddr <= aspace.base() + aspace.size() - 1);
}

static inline bool is_inside(VmAspace& aspace, VmRegion& r) {
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
    : base_(base), size_(size), flags_(flags) {

    DEBUG_ASSERT(size != 0);
    DEBUG_ASSERT(base + size - 1 >= base);

    Rename(name);

    LTRACEF("%p '%s'\n", this, name_);
}

status_t VmAspace::Init() {
    DEBUG_ASSERT(magic_ == MAGIC);

    LTRACEF("%p '%s'\n", this, name_);

    // intialize the architectually specific part
    bool is_high_kernel = (flags_ & TYPE_MASK) == TYPE_KERNEL;
    uint arch_aspace_flags = is_high_kernel ? ARCH_ASPACE_FLAG_KERNEL : 0;
    return arch_mmu_init_aspace(&arch_aspace_, base_, size_, arch_aspace_flags);
}

mxtl::RefPtr<VmAspace> VmAspace::Create(uint32_t flags, const char* name) {
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
    default:
        panic("Invalid aspace type");
    }

    AllocChecker ac;
    auto aspace = mxtl::AdoptRef(new (&ac) VmAspace(base, size, flags, name));
    if (!ac.check())
        return nullptr;

    // initialize the arch specific component to our address space
    auto err = aspace->Init();
    if (err < 0) {
        return nullptr;
    }

    // add it to the global list
    {
        AutoLock a(aspace_list_lock);
        aspaces.push_back(aspace.get());
    }

    // return a ref pointer to the aspace
    return mxtl::move(aspace);
}

void VmAspace::Rename(const char* name) {
    DEBUG_ASSERT(magic_ == MAGIC);
    strlcpy(name_, name ? name : "unnamed", sizeof(name_));
}

VmAspace::~VmAspace() {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("%p '%s'\n", this, name_);

    // we have to have already been destroyed before freeing
    DEBUG_ASSERT(regions_.is_empty());

    // pop it out of the global aspace list
    {
        AutoLock a(aspace_list_lock);
        aspaces.erase(*this);
    }

    // destroy the arch portion of the aspace
    arch_mmu_destroy_aspace(&arch_aspace_);

    // clear the magic
    magic_ = 0;
}

status_t VmAspace::Destroy() {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("%p '%s'\n", this, name_);

    // tear down and free all of the regions in our address space
    mutex_acquire(&lock_);
    mxtl::RefPtr<VmRegion> r;
    while ((r = regions_.pop_front()) != nullptr) {
        r->Unmap();

        mutex_release(&lock_);

        // free any resources the region holds
        r->Destroy();

        // Explicitly release the reference.  We don't want to cause the object
        // to destruct while in the lock.
        r.reset();

        mutex_acquire(&lock_);
    }

    mutex_release(&lock_);

    return NO_ERROR;
}

// add a region to the appropriate spot in the address space list,
// testing to see if there's a space
status_t VmAspace::AddRegion(const mxtl::RefPtr<VmRegion>& r) {
    DEBUG_ASSERT(magic_ == MAGIC);
    DEBUG_ASSERT(r);

    LTRACEF_LEVEL(2, "aspace %p base %#" PRIxPTR " size %#zx r %p base %#" PRIxPTR " size %#zx\n", this, base_,
                  size_, r.get(), r->base(), r->size());

    // only try if the region will at least fit in the address space
    if (r->size() == 0 || !is_inside(*this, *r)) {
        LTRACEF_LEVEL(2, "region was out of range\n");
        return ERR_OUT_OF_RANGE;
    }

    // regions are sorted in ascending base address order.  If this region's
    // base address is before the end of the last region's end, we will not
    // find a place for it in the list.
    vaddr_t r_end = r->base() + r->size() - 1;

    auto next_region = regions_.upper_bound(r_end);
    if (next_region != regions_.begin()) {
        auto prev_region = next_region;
        --prev_region;
        if (prev_region.IsValid()) {
            vaddr_t prev_end = prev_region->base() + prev_region->size() - 1;
            if (prev_end >= r->base()) {
                // Overlaps with previous
                LTRACEF_LEVEL(2, "couldn't find spot\n");
                return ERR_NO_MEMORY;
            }
        }
    }

    regions_.insert(r);
    return NO_ERROR;
}

//
//  Try to pick the spot within specified gap
//
//  Arch can override this to impose it's own restrictions.

__WEAK vaddr_t arch_mmu_pick_spot(arch_aspace_t* aspace, vaddr_t base,
                                  uint prev_region_arch_mmu_flags, vaddr_t end,
                                  uint next_region_arch_mmu_flags, vaddr_t align, size_t size,
                                  uint arch_mmu_flags) {
    // just align it by default
    return ALIGN(base, align);
}

//
//  Returns true if the caller has to stop search

bool VmAspace::CheckGap(const RegionTree::iterator& prev,
                        const RegionTree::iterator& next,
                        vaddr_t* pva, vaddr_t search_base, vaddr_t align, size_t region_size,
                        size_t min_gap, uint arch_mmu_flags) {
    safeint::CheckedNumeric<vaddr_t> gap_beg; // first byte of a gap
    safeint::CheckedNumeric<vaddr_t> gap_end; // last byte of a gap
    vaddr_t real_gap_beg = 0;
    vaddr_t real_gap_end = 0;

    DEBUG_ASSERT(pva);

    // compute the starting address of the gap
    if (prev.IsValid()) {
        gap_beg = prev->base();
        gap_beg += prev->size();
        gap_beg += min_gap;
    } else {
        gap_beg = base_;
    }

    if (!gap_beg.IsValid())
        goto not_found;
    real_gap_beg = gap_beg.ValueOrDie();

    // compute the ending address of the gap
    if (next.IsValid()) {
        if (real_gap_beg == next->base())
            goto next_gap; // no gap between regions
        gap_end = next->base();
        gap_end -= 1;
        gap_end -= min_gap;
    } else {
        if (real_gap_beg == base_ + size_)
            goto not_found; // no gap at the end of address space. Stop search
        gap_end = base_;
        gap_end += size_ - 1;
    }

    if (!gap_end.IsValid())
        goto not_found;
    real_gap_end = gap_end.ValueOrDie();

    DEBUG_ASSERT(real_gap_end > real_gap_beg);

    // trim it to the search range
    if (real_gap_end <= search_base)
        return false;
    if (real_gap_beg < search_base)
        real_gap_beg = search_base;

    DEBUG_ASSERT(real_gap_end > real_gap_beg);

    LTRACEF_LEVEL(2, "search base %#" PRIxPTR " real_gap_beg %#" PRIxPTR " end %#" PRIxPTR "\n", search_base, real_gap_beg, real_gap_end);

    *pva = arch_mmu_pick_spot(&arch_aspace(), real_gap_beg,
                              prev.IsValid() ? prev->arch_mmu_flags() : ARCH_MMU_FLAG_INVALID,
                              real_gap_end,
                              next.IsValid() ? next->arch_mmu_flags() : ARCH_MMU_FLAG_INVALID,
                              align, region_size, arch_mmu_flags);
    if (*pva < real_gap_beg)
        goto not_found; // address wrapped around

    if (*pva < real_gap_end && ((real_gap_end - *pva + 1) >= region_size)) {
        // we have enough room
        return true; // found spot, stop search
    }

next_gap:
    return false; // continue search

not_found:
    *pva = -1;
    return true; // not_found: stop search
}

// search for a spot to allocate for a region of a given size, returning an
// iterator to the region after it in the list.
vaddr_t VmAspace::AllocSpot(vaddr_t base, size_t size, uint8_t align_pow2, size_t min_alloc_gap,
                            uint arch_mmu_flags) {
    DEBUG_ASSERT(magic_ == MAGIC);
    DEBUG_ASSERT(size > 0 && IS_PAGE_ALIGNED(size));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(min_alloc_gap));
    DEBUG_ASSERT(min_alloc_gap <= MAX_MIN_ALLOC_GAP);
    DEBUG_ASSERT(is_mutex_held(&lock_));

    LTRACEF_LEVEL(2, "aspace %p base %#" PRIxPTR " size 0x%zx align %hhu\n", this, base, size, align_pow2);

    if (align_pow2 < PAGE_SIZE_SHIFT)
        align_pow2 = PAGE_SIZE_SHIFT;
    vaddr_t align = 1UL << align_pow2;

    vaddr_t spot;

    // Find the first gap in the address space which can contain a region of the requested size.
    auto before_iter = regions_.end();
    auto after_iter = regions_.begin();

    do {
        if (CheckGap(before_iter, after_iter, &spot, base, align, size, min_alloc_gap, arch_mmu_flags)) {
            return spot;
        }

        before_iter = after_iter++;
    } while (before_iter.IsValid());

    // couldn't find anything
    return -1;
}

// internal find region search routine
mxtl::RefPtr<VmRegion> VmAspace::FindRegionLocked(vaddr_t vaddr) {
    DEBUG_ASSERT(magic_ == MAGIC);
    DEBUG_ASSERT(is_mutex_held(&lock_));

    // Find the region that includes vaddr + 1 or further, ours should be
    // before it.
    auto iter = regions_.upper_bound(vaddr);
    --iter;

    // If iter is invalid, the upper bound was either the beginning of the list,
    // or the end of an empty list.
    if (!iter.IsValid()) {
        return nullptr;
    }

    if ((vaddr >= iter->base()) && (vaddr <= iter->base() + iter->size() - 1))
        return iter.CopyPointer();

    return nullptr;
}

// return a ref pointer to a region
mxtl::RefPtr<VmRegion> VmAspace::FindRegion(vaddr_t vaddr) {
    AutoLock a(lock_);
    return FindRegionLocked(vaddr);
}

status_t VmAspace::MapObject(mxtl::RefPtr<VmObject> vmo, const char* name, uint64_t offset,
                             size_t size, void** ptr, uint8_t align_pow2, size_t min_alloc_gap,
                             uint vmm_flags, uint arch_mmu_flags) {

    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF(
        "aspace %p name '%s' vmo %p, offset %#" PRIx64 " size %#zx "
        "ptr %p align %hhu vmm_flags %#x arch_mmu_flags %#x\n",
        this, name, vmo.get(), offset, size, ptr ? *ptr : 0, align_pow2, vmm_flags, arch_mmu_flags);

    size = ROUNDUP(size, PAGE_SIZE);
    if (size == 0)
        return ERR_INVALID_ARGS;
    if (!vmo)
        return ERR_INVALID_ARGS;
    if (!IS_PAGE_ALIGNED(offset))
        return ERR_INVALID_ARGS;

    vaddr_t vaddr = 0;
    // if they're asking for a specific spot or starting address, copy the address
    if (vmm_flags & (VMM_FLAG_VALLOC_SPECIFIC | VMM_FLAG_VALLOC_BASE)) {
        // can't ask for a specific spot and then not provide one
        if (!ptr) {
            return ERR_INVALID_ARGS;
        }
        vaddr = reinterpret_cast<vaddr_t>(*ptr);

        // check that it's page aligned
        if (!IS_PAGE_ALIGNED(vaddr))
            return ERR_INVALID_ARGS;
    }

    // allocate a region and put it in the aspace list
    mxtl::RefPtr<VmRegion> r = VmRegion::Create(*this, vaddr, size, vmo, offset, arch_mmu_flags, name);
    if (!r)
        return ERR_NO_MEMORY;

    // hold the aspace lock for the rest of the function
    AutoLock a(lock_);

    // if they ask us for a specific spot, put it there
    if (vmm_flags & VMM_FLAG_VALLOC_SPECIFIC) {
        DEBUG_ASSERT(IS_PAGE_ALIGNED(vaddr));

        // stick it in the list, checking to see if it fits
        if (AddRegion(r) < 0) {
            // didn't fit
            return ERR_NO_MEMORY;
        }
    } else {
        // allocate a virtual slot for it
        RegionTree::iterator after;
        vaddr_t base = (vmm_flags & VMM_FLAG_VALLOC_BASE) ? vaddr : 0;
        vaddr = AllocSpot(base, size, align_pow2, min_alloc_gap, arch_mmu_flags);
        LTRACEF_LEVEL(2, "alloc_spot returns %#" PRIxPTR "\n", vaddr);

        if (vaddr == (vaddr_t)-1) {
            LTRACEF_LEVEL(2, "failed to find spot\n");
            return ERR_NO_MEMORY;
        }

        r->set_base(vaddr);

        // add it to the region list
        regions_.insert(r);
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

    return NO_ERROR;
}

status_t VmAspace::ReserveSpace(const char* name, size_t size, vaddr_t vaddr) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("aspace %p name '%s' size %#zx vaddr %#" PRIxPTR "\n",
            this, name, size, vaddr);

    DEBUG_ASSERT(IS_PAGE_ALIGNED(vaddr));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(size));

    size = ROUNDUP_PAGE_SIZE(size);
    if (size == 0)
        return NO_ERROR;
    if (!IS_PAGE_ALIGNED(vaddr))
        return ERR_INVALID_ARGS;
    if (!is_inside(*this, vaddr))
        return ERR_OUT_OF_RANGE;

    // trim the size
    size = trim_to_aspace(*this, vaddr, size);

    // allocate a zero length vm object to back it
    // TODO: decide if a null vmo object is worth it
    auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0);
    if (!vmo)
        return ERR_NO_MEMORY;

    // lookup how it's already mapped
    uint arch_mmu_flags = 0;
    auto err = arch_mmu_query(&arch_aspace_, vaddr, nullptr, &arch_mmu_flags);
    if (err) {
        // if it wasn't already mapped, use some sort of strict default
        arch_mmu_flags = ARCH_MMU_FLAG_CACHED | ARCH_MMU_FLAG_PERM_READ;
    }

    // map it, creating a new region
    void *ptr = reinterpret_cast<void *>(vaddr);
    return MapObject(mxtl::move(vmo), name, 0, size, &ptr, 0, 0, VMM_FLAG_VALLOC_SPECIFIC, arch_mmu_flags);
}

status_t VmAspace::AllocPhysical(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                                 size_t min_alloc_gap, paddr_t paddr,
                                 uint vmm_flags, uint arch_mmu_flags) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF(
        "aspace %p name '%s' size %#zx ptr %p paddr %#" PRIxPTR
        " vmm_flags 0x%x arch_mmu_flags 0x%x\n",
        this, name, size, ptr ? *ptr : 0, paddr, vmm_flags, arch_mmu_flags);

    DEBUG_ASSERT(IS_PAGE_ALIGNED(paddr));

    if (size == 0)
        return NO_ERROR;
    if (!IS_PAGE_ALIGNED(paddr))
        return ERR_INVALID_ARGS;

    size = ROUNDUP_PAGE_SIZE(size);

    // create a vm object to back it
    auto vmo = VmObjectPhysical::Create(paddr, size);
    if (!vmo)
        return ERR_NO_MEMORY;

    // force it to be mapped up front
    // TODO: add new flag to precisely mean pre-map
    vmm_flags |= VMM_FLAG_COMMIT;

    return MapObject(mxtl::move(vmo), name, 0, size, ptr, align_pow2, min_alloc_gap, vmm_flags, arch_mmu_flags);
}

status_t VmAspace::AllocContiguous(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                                   size_t min_alloc_gap, uint vmm_flags, uint arch_mmu_flags) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("aspace %p name '%s' size 0x%zx ptr %p align %hhu vmm_flags 0x%x arch_mmu_flags 0x%x\n",
            this, name, size, ptr ? *ptr : 0, align_pow2, vmm_flags, arch_mmu_flags);

    size = ROUNDUP(size, PAGE_SIZE);
    if (size == 0)
        return ERR_INVALID_ARGS;

    // test for invalid flags
    if (!(vmm_flags & VMM_FLAG_COMMIT))
        return ERR_INVALID_ARGS;

    // create a vm object to back it
    auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, size);
    if (!vmo)
        return ERR_NO_MEMORY;

    // always immediately commit memory to the object
    int64_t committed = vmo->CommitRangeContiguous(0, size, align_pow2);
    if (committed < 0 || (size_t)committed < size) {
        LTRACEF("failed to allocate enough pages (asked for %zu, got %zu)\n", size / PAGE_SIZE,
                (size_t)committed / PAGE_SIZE);
        return ERR_NO_MEMORY;
    }

    return MapObject(mxtl::move(vmo), name, 0, size, ptr, align_pow2, min_alloc_gap, vmm_flags, arch_mmu_flags);
}

status_t VmAspace::Alloc(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                         size_t min_alloc_gap, uint vmm_flags, uint arch_mmu_flags) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("aspace %p name '%s' size 0x%zx ptr %p align %hhu vmm_flags 0x%x arch_mmu_flags 0x%x\n",
            this, name, size, ptr ? *ptr : 0, align_pow2, vmm_flags, arch_mmu_flags);

    size = ROUNDUP(size, PAGE_SIZE);
    if (size == 0)
        return ERR_INVALID_ARGS;

    // allocate a vm object to back it
    auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, size);
    if (!vmo)
        return ERR_NO_MEMORY;

    // commit memory up front if requested
    if (vmm_flags & VMM_FLAG_COMMIT) {
        // commit memory to the object
        int64_t committed = vmo->CommitRange(0, size);
        if (committed < 0 || (size_t)committed < size) {
            LTRACEF("failed to allocate enough pages (asked for %zu, got %zu)\n", size / PAGE_SIZE,
                    (size_t)committed / PAGE_SIZE);
            return ERR_NO_MEMORY;
        }
    }

    // map it, creating a new region
    return MapObject(mxtl::move(vmo), name, 0, size, ptr, align_pow2, min_alloc_gap, vmm_flags, arch_mmu_flags);
}

status_t VmAspace::FreeRegion(vaddr_t vaddr) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("vaddr %#" PRIxPTR "\n", vaddr);

    mxtl::RefPtr<VmRegion> r;
    {
        AutoLock a(lock_);

        r = FindRegionLocked(vaddr);
        if (!r)
            return ERR_NOT_FOUND;

        // remove it from the address space list
        regions_.erase(*r);

        // unmap it
        r->Unmap();
    }

    // destroy the region
    r->Destroy();

    return NO_ERROR;
}

void VmAspace::AttachToThread(thread_t* t) {
    DEBUG_ASSERT(magic_ == MAGIC);
    DEBUG_ASSERT(t);

    // point the lk thread at our object via the dummy C vmm_aspace_t struct
    THREAD_LOCK(state);

    // not prepared to handle setting a new address space or one on a running thread
    DEBUG_ASSERT(!t->aspace);
    DEBUG_ASSERT(t->state != THREAD_RUNNING);

    t->aspace = reinterpret_cast<vmm_aspace_t*>(this);
    THREAD_UNLOCK(state);
}

status_t VmAspace::PageFault(vaddr_t va, uint flags) {
    DEBUG_ASSERT(magic_ == MAGIC);
    LTRACEF("va %#" PRIxPTR ", flags %#x\n", va, flags);

    // for now, hold the aspace lock across the page fault operation,
    // which stops any other operations on the address space from moving
    // the region out from underneath it
    AutoLock a(lock_);

    auto r = FindRegionLocked(va);
    if (unlikely(!r))
        return ERR_NOT_FOUND;

    return r->PageFault(va, flags);
}

void VmAspace::Dump() const {
    DEBUG_ASSERT(magic_ == MAGIC);
    printf("aspace %p: ref %d name '%s' range %#" PRIxPTR " - %#" PRIxPTR
           " size %#zx flags %#x\n",
           this,
           ref_count_debug(), name_, base_, base_ + size_ - 1, size_, flags_);

    printf("regions:\n");
    AutoLock a(lock_);
    for (const auto& r : regions_) {
        r.Dump();
    }
}

void DumpAllAspaces() {
    AutoLock a(aspace_list_lock);

    for (const auto& a : aspaces)
        a.Dump();
}

size_t VmAspace::AllocatedPages() const {
    DEBUG_ASSERT(magic_ == MAGIC);
    AutoLock a(lock_);
    size_t result = 0;
    for (const auto& r : regions_) {
        result += r.AllocatedPages();
    }
    return result;
}
