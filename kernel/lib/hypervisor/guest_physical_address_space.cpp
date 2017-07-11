// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <hypervisor/guest_physical_address_space.h>

#include <arch/mmu.h>
#include <kernel/vm/arch_vm_aspace.h>
#include <kernel/vm/fault.h>
#include <kernel/vm/vm_object_physical.h>
#include <mxalloc/new.h>

static const uint kPfFlags = VMM_PF_FLAG_WRITE | VMM_PF_FLAG_SW_FAULT;
static const uint kApicMmuFlags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
static const uint kMmuFlags =
    ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE;

namespace {
// Locate a VMO for a given vaddr.
struct AspaceVmoLocator final : public VmEnumerator {
    AspaceVmoLocator(vaddr_t vaddr_)
        : vaddr(vaddr_) {}

    bool OnVmMapping(const VmMapping* map, const VmAddressRegion* vmar,
                     uint depth) final {
        if (vaddr < map->base() || vaddr >= (map->base() + map->size())) {
            // Mapping does not cover 'vaddr', return true to keep going.
            return true;
        }
        vmo = map->vmo();
        base = map->base();
        return false;
    }

    mxtl::RefPtr<VmObject> vmo = nullptr;
    vaddr_t base = 0;
    const vaddr_t vaddr;
};
} // namespace

status_t GuestPhysicalAddressSpace::Create(mxtl::RefPtr<VmObject> guest_phys_mem,
                                           mxtl::unique_ptr<GuestPhysicalAddressSpace>* _gpas) {
    AllocChecker ac;
    mxtl::unique_ptr<GuestPhysicalAddressSpace> gpas(
        new (&ac) GuestPhysicalAddressSpace(guest_phys_mem));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    status_t status = gpas->Init(guest_phys_mem);
    if (status != MX_OK)
        return status;

    *_gpas = mxtl::move(gpas);
    return MX_OK;
}

status_t GuestPhysicalAddressSpace::Init(mxtl::RefPtr<VmObject> root_vmo) {
    paspace_ = VmAspace::Create(VmAspace::TYPE_GUEST_PHYS, "guest_paspace");
    if (!paspace_)
        return MX_ERR_NO_MEMORY;

    // Initialize our VMAR with the provided VMO, mapped at address 0.
    mxtl::RefPtr<VmMapping> mapping;
    status_t result = paspace_->RootVmar()->CreateVmMapping(
        0 /* mapping_offset */, root_vmo->size(), /* align_pow2*/ 0,
        VMAR_FLAG_SPECIFIC, root_vmo, /* vmo_offset */ 0,
        kMmuFlags, "guest_phys_mem_vmo", &mapping);
    if (result != MX_OK)
        return result;

    return MX_OK;
}

GuestPhysicalAddressSpace::GuestPhysicalAddressSpace(mxtl::RefPtr<VmObject> guest_phys_mem)
    : guest_phys_mem_(guest_phys_mem) {}

GuestPhysicalAddressSpace::~GuestPhysicalAddressSpace() {
    // VmAspace maintains a circular reference with it's root VMAR. We need to
    // destroy the VmAspace in order to break that reference and allow the
    // VmAspace to be destructed.
    if (paspace_)
        paspace_->Destroy();
}

status_t GuestPhysicalAddressSpace::MapApicPage(vaddr_t guest_paddr, paddr_t host_paddr) {
    mxtl::RefPtr<VmObject> vmo;
    status_t result = VmObjectPhysical::Create(host_paddr, PAGE_SIZE, &vmo);
    if (result != MX_OK)
        return result;

    result = vmo->SetMappingCachePolicy(ARCH_MMU_FLAG_CACHED);
    if (result != MX_OK)
        return result;

    // The root VMAR will maintain a reference to the VmMapping internally so
    // we don't need to maintain a long-lived reference to the mapping here.
    mxtl::RefPtr<VmMapping> mapping;
    result = paspace_->RootVmar()->CreateVmMapping(
        guest_paddr, vmo->size(), /* align_pow2*/ 0,
        VMAR_FLAG_SPECIFIC, vmo, /* vmo_offset */ 0,
        kApicMmuFlags, "guest_apic_vmo", &mapping);
    if (result != MX_OK)
        return result;

    // Write mapping to page table.
    result = mapping->MapRange(0, vmo->size(), true);
    if (result != MX_OK) {
        mapping->Destroy();
        return result;
    }
    return MX_OK;
}

status_t GuestPhysicalAddressSpace::UnmapRange(vaddr_t guest_paddr, size_t size) {
    return paspace_->RootVmar()->Unmap(guest_paddr, size);
}

status_t GuestPhysicalAddressSpace::GetPage(vaddr_t guest_paddr, paddr_t* host_paddr) {
    // Locate the VMO for the guest physical address (if present).
    AspaceVmoLocator vmo_locator(guest_paddr);
    paspace_->EnumerateChildren(&vmo_locator);
    mxtl::RefPtr<VmObject> vmo = vmo_locator.vmo;
    if (!vmo)
        return MX_ERR_NOT_FOUND;

    // Lookup the physical address of this page in the VMO.
    vaddr_t offset = guest_paddr - vmo_locator.base;
    auto get_page = [](void* context, size_t offset, size_t index, paddr_t pa) -> status_t {
        *static_cast<paddr_t*>(context) = pa;
        return MX_OK;
    };
    return vmo->Lookup(offset, PAGE_SIZE, kPfFlags, get_page, host_paddr);
}
