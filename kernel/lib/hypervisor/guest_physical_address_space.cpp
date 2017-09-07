// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <hypervisor/guest_physical_address_space.h>

#include <arch/mmu.h>
#include <vm/arch_vm_aspace.h>
#include <vm/fault.h>
#include <vm/vm_object_physical.h>
#include <fbl/alloc_checker.h>

static const uint kPfFlags = VMM_PF_FLAG_WRITE | VMM_PF_FLAG_SW_FAULT;
static const uint kMmuFlags =
    ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE;

#if ARCH_X86_64
static const uint kApicMmuFlags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
#endif // ARCH_X86_64

namespace {
// Locate a VMO for a given vaddr.
struct AspaceVmoLocator final : public VmEnumerator {
    AspaceVmoLocator(vaddr_t vaddr_) : vaddr(vaddr_) {}

    bool OnVmMapping(const VmMapping* map, const VmAddressRegion* vmar, uint depth) final {
        if (vaddr < map->base() || vaddr >= (map->base() + map->size())) {
            // Mapping does not cover 'vaddr', return true to keep going.
            return true;
        }
        vmo = map->vmo();
        base = map->base();
        return false;
    }

    fbl::RefPtr<VmObject> vmo = nullptr;
    vaddr_t base = 0;
    const vaddr_t vaddr;
};
} // namespace

mx_status_t GuestPhysicalAddressSpace::Create(fbl::RefPtr<VmObject> guest_phys_mem,
                                              fbl::unique_ptr<GuestPhysicalAddressSpace>* _gpas) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<GuestPhysicalAddressSpace> gpas(new (&ac)
                                                         GuestPhysicalAddressSpace(guest_phys_mem));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    gpas->paspace_ = VmAspace::Create(VmAspace::TYPE_GUEST_PHYS, "guest_paspace");
    if (!gpas->paspace_)
        return MX_ERR_NO_MEMORY;

    // Initialize our VMAR with the provided VMO, mapped at address 0.
    fbl::RefPtr<VmMapping> mapping;
    mx_status_t result = gpas->paspace_->RootVmar()->CreateVmMapping(
        0 /* mapping_offset */, guest_phys_mem->size(), /* align_pow2*/ 0, VMAR_FLAG_SPECIFIC,
        guest_phys_mem, /* vmo_offset */ 0, kMmuFlags, "guest_phys_mem_vmo", &mapping);
    if (result != MX_OK)
        return result;

    *_gpas = fbl::move(gpas);
    return MX_OK;
}

GuestPhysicalAddressSpace::GuestPhysicalAddressSpace(fbl::RefPtr<VmObject> guest_phys_mem)
    : guest_phys_mem_(guest_phys_mem) {}

GuestPhysicalAddressSpace::~GuestPhysicalAddressSpace() {
    // VmAspace maintains a circular reference with it's root VMAR. We need to
    // destroy the VmAspace in order to break that reference and allow the
    // VmAspace to be destructed.
    if (paspace_)
        paspace_->Destroy();
}

#if ARCH_X86_64
mx_status_t GuestPhysicalAddressSpace::MapApicPage(vaddr_t guest_paddr, paddr_t host_paddr) {
    fbl::RefPtr<VmObject> vmo;
    mx_status_t result = VmObjectPhysical::Create(host_paddr, PAGE_SIZE, &vmo);
    if (result != MX_OK)
        return result;

    result = vmo->SetMappingCachePolicy(ARCH_MMU_FLAG_CACHED);
    if (result != MX_OK)
        return result;

    // The root VMAR will maintain a reference to the VmMapping internally so
    // we don't need to maintain a long-lived reference to the mapping here.
    fbl::RefPtr<VmMapping> mapping;
    result = paspace_->RootVmar()->CreateVmMapping(guest_paddr, vmo->size(), /* align_pow2*/ 0,
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
#endif // ARCH_X86_64

mx_status_t GuestPhysicalAddressSpace::UnmapRange(vaddr_t guest_paddr, size_t size) {
    return paspace_->RootVmar()->Unmap(guest_paddr, size);
}

mx_status_t GuestPhysicalAddressSpace::GetPage(vaddr_t guest_paddr, paddr_t* host_paddr) {
    // Locate the VMO for the guest physical address (if present).
    AspaceVmoLocator vmo_locator(guest_paddr);
    paspace_->EnumerateChildren(&vmo_locator);
    fbl::RefPtr<VmObject> vmo = vmo_locator.vmo;
    if (!vmo)
        return MX_ERR_NOT_FOUND;

    // Lookup the physical address of this page in the VMO.
    vaddr_t offset = guest_paddr - vmo_locator.base;
    return vmo->Lookup(offset, PAGE_SIZE, kPfFlags, guest_lookup_page, host_paddr);
}
