// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/io_mapping_dispatcher.h>

#include <kernel/vm/vm_object_physical.h>
#include <magenta/process_dispatcher.h>
#include <mxalloc/new.h>

constexpr mx_rights_t IoMappingDispatcher::kDefaultRights;

status_t IoMappingDispatcher::Create(const char* dbg_name,
                                     paddr_t paddr, size_t size,
                                     uint vmm_flags, uint arch_mmu_flags,
                                     mxtl::RefPtr<Dispatcher>* out_dispatcher,
                                     mx_rights_t* out_rights) {
    if (!out_dispatcher || !out_rights)
        return ERR_INVALID_ARGS;

    AllocChecker ac;
    IoMappingDispatcher* disp = new (&ac) IoMappingDispatcher();
    if (!ac.check())
        return ERR_NO_MEMORY;

    status_t status;
    status = disp->Init(dbg_name, paddr, size, vmm_flags, arch_mmu_flags);
    if (status != NO_ERROR) {
        delete disp;
    } else {
        *out_dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
        *out_rights     = kDefaultRights;
    }

    return status;
}

IoMappingDispatcher::~IoMappingDispatcher() {
    Cleanup();
}

void IoMappingDispatcher::Close() {
    Cleanup();
}

void IoMappingDispatcher::Cleanup() {
    canary_.Assert();

    if (mapping_) {
        mapping_->Destroy();
    }

    vaddr_ = 0;
    aspace_.reset();
    mapping_.reset();
}

bool IoMappingDispatcher::closed() const {
    canary_.Assert();
    return !aspace_ || !vaddr_;
}

status_t IoMappingDispatcher::Init(const char* dbg_name,
                                   paddr_t paddr, size_t size,
                                   uint vmm_flags, uint arch_mmu_flags) {
    DEBUG_ASSERT(closed());

    // TODO(teisenbe): Remove vmm_flags
    DEBUG_ASSERT(!vmm_flags);

    if (!IS_ALIGNED(paddr, PAGE_SIZE) ||
        !IS_ALIGNED(size,  PAGE_SIZE) ||
        !size)
        return ERR_INVALID_ARGS;

    aspace_ = ProcessDispatcher::GetCurrent()->aspace();
    DEBUG_ASSERT(aspace_); // This should never fail.
    if (!aspace_)
        return ERR_INTERNAL;

    mxtl::RefPtr<VmObject> vmo(VmObjectPhysical::Create(paddr, size));
    if (!vmo)
        return ERR_NO_MEMORY;

    paddr_ = paddr;
    size_ = size;

    uint vmo_cache_flags = arch_mmu_flags & ARCH_MMU_FLAG_CACHE_MASK;
    arch_mmu_flags &= ~ARCH_MMU_FLAG_CACHE_MASK;
    if (vmo->SetMappingCachePolicy(vmo_cache_flags) != NO_ERROR)
        return ERR_INVALID_ARGS;

    auto root_vmar = aspace_->RootVmar();
    status_t res = root_vmar->CreateVmMapping(0, size, PAGE_SIZE_SHIFT, 0,
                                              mxtl::move(vmo), 0, arch_mmu_flags,
                                              dbg_name, &mapping_);
    if (res != NO_ERROR)
        return res;

    // Force the entries into the page tables
    res = mapping_->MapRange(0, size, false);
    if (res < 0) {
        mapping_->Destroy();
        mapping_.reset();
        return res;
    }

    vaddr_ = mapping_->base();
    return NO_ERROR;
}
