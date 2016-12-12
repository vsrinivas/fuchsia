// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/vm/vm_address_region.h>

#include "vm_priv.h"
#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <mxtl/auto_call.h>
#include <mxtl/auto_lock.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

VmAddressRegionOrMapping::VmAddressRegionOrMapping(uint32_t magic,
                                                   vaddr_t base, size_t size, uint32_t flags,
                                                   VmAspace& aspace, VmAddressRegion* parent,
                                                   const char* name)
    : magic_(magic), state_(LifeCycleState::NOT_READY), base_(base), size_(size), flags_(flags),
      aspace_(&aspace), parent_(parent) {

    strlcpy(name_, name, sizeof(name_));
    LTRACEF("%p '%s'\n", this, name_);
}

status_t VmAddressRegionOrMapping::Destroy() {
    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return ERR_BAD_STATE;
    }

    return DestroyLocked();
}

VmAddressRegionOrMapping::~VmAddressRegionOrMapping() {
    LTRACEF("%p '%s'\n", this, name_);

    if (state_ == LifeCycleState::ALIVE) {
        Destroy();
    }

    DEBUG_ASSERT(!subregion_list_node_.InContainer());

    // clear the magic
    magic_ = 0;
}

mxtl::RefPtr<VmAddressRegion> VmAddressRegionOrMapping::as_vm_address_region() {
    if (is_mapping()) {
        return nullptr;
    }
    return mxtl::RefPtr<VmAddressRegion>(static_cast<VmAddressRegion*>(this));
}

mxtl::RefPtr<VmMapping> VmAddressRegionOrMapping::as_vm_mapping() {
    if (!is_mapping()) {
        return nullptr;
    }
    return mxtl::RefPtr<VmMapping>(static_cast<VmMapping*>(this));
}

bool VmAddressRegionOrMapping::is_valid_mapping_flags(uint arch_mmu_flags) {
    if (!(flags_ & VMAR_FLAG_CAN_MAP_READ) && (arch_mmu_flags & ARCH_MMU_FLAG_PERM_READ)) {
        return false;
    }
    if (!(flags_ & VMAR_FLAG_CAN_MAP_WRITE) && (arch_mmu_flags & ARCH_MMU_FLAG_PERM_WRITE)) {
        return false;
    }
    if (!(flags_ & VMAR_FLAG_CAN_MAP_EXECUTE) && (arch_mmu_flags & ARCH_MMU_FLAG_PERM_EXECUTE)) {
        return false;
    }
    return true;
}

size_t VmAddressRegionOrMapping::AllocatedPages() const {
    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return 0;
    }
    return AllocatedPagesLocked();
}
