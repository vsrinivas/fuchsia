// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <hypervisor/guest_physical_address_space.h>

#include <kernel/vm/fault.h>
#include <mxalloc/new.h>

static const uint kPfFlags = VMM_PF_FLAG_WRITE | VMM_PF_FLAG_SW_FAULT;
static const uint kApicMmuFlags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
static const uint kMmuFlags =
    ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE;
static const size_t kAddressSpaceSize =  256ul << 30;

status_t GuestPhysicalAddressSpace::Create(mxtl::RefPtr<VmObject> guest_phys_mem,
                                           mxtl::unique_ptr<GuestPhysicalAddressSpace>* _gpas) {
    AllocChecker ac;
    mxtl::unique_ptr<GuestPhysicalAddressSpace> gpas(
        new (&ac) GuestPhysicalAddressSpace(guest_phys_mem));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    status_t status = guest_mmu_init_paspace(&gpas->paspace_, kAddressSpaceSize);
    if (status != MX_OK)
        return status;

    // TODO(abdulla): Figure out how to do this on-demand.
    status = gpas->MapRange(0, guest_phys_mem->size());
    if (status != MX_OK)
        return status;

    *_gpas = mxtl::move(gpas);
    return MX_OK;
}

GuestPhysicalAddressSpace::GuestPhysicalAddressSpace(mxtl::RefPtr<VmObject> guest_phys_mem)
    : guest_phys_mem_(guest_phys_mem) {}

GuestPhysicalAddressSpace::~GuestPhysicalAddressSpace() {
    __UNUSED status_t status = guest_mmu_destroy_paspace(&paspace_);
    DEBUG_ASSERT(status == MX_OK);
}

static status_t map_page(guest_paspace_t* paspace, vaddr_t guest_paddr, paddr_t host_paddr,
                         uint mmu_flags) {
    size_t mapped;
    status_t status = guest_mmu_map(paspace, guest_paddr, host_paddr, 1, mmu_flags, &mapped);
    if (status != MX_OK)
        return status;
    return mapped != 1 ? MX_ERR_NO_MEMORY : MX_OK;
}

status_t GuestPhysicalAddressSpace::MapApicPage(vaddr_t guest_paddr, paddr_t host_paddr) {
    return map_page(&paspace_, guest_paddr, host_paddr, kApicMmuFlags);
}

status_t GuestPhysicalAddressSpace::MapRange(vaddr_t guest_paddr, size_t size) {
    auto mmu_map = [](void* context, size_t offset, size_t index, paddr_t pa) -> status_t {
        guest_paspace_t* paspace = static_cast<guest_paspace_t*>(context);
        return map_page(paspace, offset, pa, kMmuFlags);
    };
    return guest_phys_mem_->Lookup(guest_paddr, size, kPfFlags, mmu_map, &paspace_);
}

status_t GuestPhysicalAddressSpace::UnmapRange(vaddr_t guest_paddr, size_t size) {
    size_t num_pages = size / PAGE_SIZE;
    size_t unmapped;
    status_t status = guest_mmu_unmap(&paspace_, guest_paddr, num_pages, &unmapped);
    if (status != MX_OK)
        return status;
    return unmapped != num_pages ? MX_ERR_BAD_STATE : MX_OK;
}

status_t GuestPhysicalAddressSpace::GetPage(vaddr_t guest_paddr, paddr_t* host_paddr) {
    uint mmu_flags;
    return guest_mmu_query(&paspace_, guest_paddr, host_paddr, &mmu_flags);
}
