// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <new.h>

#include <hypervisor/guest_physical_address_space.h>
#include <safeint/safe_math.h>

static const uint kPfFlags = VMM_PF_FLAG_WRITE | VMM_PF_FLAG_SW_FAULT;
static const uint kMmuFlags =
    ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE;

status_t GuestPhysicalAddressSpace::Create(mxtl::RefPtr<VmObject> guest_phys_mem,
                                           mxtl::unique_ptr<GuestPhysicalAddressSpace>* _gpas) {
    AllocChecker ac;
    mxtl::unique_ptr<GuestPhysicalAddressSpace> gpas(
        new (&ac) GuestPhysicalAddressSpace(guest_phys_mem));
    if (!ac.check())
        return ERR_NO_MEMORY;

    size_t size = guest_phys_mem->size();
    status_t status = guest_mmu_init_paspace(&gpas->paspace_, size);
    if (status != NO_ERROR)
        return status;

    // TODO(abdulla): Figure out how to do this on-demand.
    status = gpas->MapRange(0, size);
    if (status != NO_ERROR)
        return status;

    *_gpas = mxtl::move(gpas);
    return NO_ERROR;
}

GuestPhysicalAddressSpace::GuestPhysicalAddressSpace(mxtl::RefPtr<VmObject> guest_phys_mem)
    : guest_phys_mem_(guest_phys_mem) {}

GuestPhysicalAddressSpace::~GuestPhysicalAddressSpace() {
    __UNUSED status_t status = guest_mmu_destroy_paspace(&paspace_);
    DEBUG_ASSERT(status == NO_ERROR);
}

status_t GuestPhysicalAddressSpace::MapRange(size_t offset, size_t len) {
    auto mmu_map = [](void* context, size_t offset, size_t index, paddr_t pa) -> status_t {
        guest_paspace_t* paspace = static_cast<guest_paspace_t*>(context);
        size_t mapped;
        status_t status = guest_mmu_map(paspace, offset, pa, 1, kMmuFlags, &mapped);
        if (status != NO_ERROR)
            return status;
        return mapped != 1 ? ERR_NO_MEMORY : NO_ERROR;
    };
    return guest_phys_mem_->Lookup(offset, len, kPfFlags, mmu_map, &paspace_);
}
