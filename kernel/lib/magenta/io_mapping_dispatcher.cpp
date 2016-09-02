// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/io_mapping_dispatcher.h>
#include <magenta/process_dispatcher.h>

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
    if (vaddr_) {
        DEBUG_ASSERT(aspace_);
        aspace_->FreeRegion(vaddr_);
    }

    vaddr_ = 0;
    aspace_.reset();
}

bool IoMappingDispatcher::closed() const {
    return !aspace_ || !vaddr_;
}

status_t IoMappingDispatcher::Init(const char* dbg_name,
                                   paddr_t paddr, size_t size,
                                   uint vmm_flags, uint arch_mmu_flags) {
    DEBUG_ASSERT(closed());

    if (!IS_ALIGNED(paddr, PAGE_SIZE) ||
        !IS_ALIGNED(size,  PAGE_SIZE) ||
        !size)
        return ERR_INVALID_ARGS;

    aspace_ = ProcessDispatcher::GetCurrent()->aspace();
    DEBUG_ASSERT(aspace_); // This should never fail.
    if (!aspace_)
        return ERR_INTERNAL;

    paddr_ = paddr;
    size_  = size;

    return aspace_->AllocPhysical(dbg_name,
                                  size,
                                  reinterpret_cast<void**>(&vaddr_),
                                  PAGE_SIZE_SHIFT,
                                  0,
                                  paddr,
                                  vmm_flags,
                                  arch_mmu_flags);
}
