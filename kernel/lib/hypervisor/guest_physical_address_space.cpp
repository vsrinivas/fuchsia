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

static constexpr uint kPfFlags = VMM_PF_FLAG_WRITE | VMM_PF_FLAG_SW_FAULT;
static constexpr uint kMmuFlags =
    ARCH_MMU_FLAG_CACHED |
    ARCH_MMU_FLAG_PERM_READ |
    ARCH_MMU_FLAG_PERM_WRITE |
    ARCH_MMU_FLAG_PERM_EXECUTE;

static constexpr uint kInterruptMmuFlags =
    ARCH_MMU_FLAG_PERM_READ |
    ARCH_MMU_FLAG_PERM_WRITE;

static constexpr uint kGuestMmuFlags =
    ARCH_MMU_FLAG_CACHED |
    ARCH_MMU_FLAG_PERM_READ |
    ARCH_MMU_FLAG_PERM_WRITE;

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

namespace hypervisor {

zx_status_t GuestPhysicalAddressSpace::Create(fbl::RefPtr<VmObject> guest_phys_mem,
#if ARCH_ARM64
                                              uint8_t vmid,
#endif
                                              fbl::unique_ptr<GuestPhysicalAddressSpace>* _gpas) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<GuestPhysicalAddressSpace> gpas(new (&ac)
                                                         GuestPhysicalAddressSpace(guest_phys_mem));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    gpas->paspace_ = VmAspace::Create(VmAspace::TYPE_GUEST_PHYS, "guest_paspace");
    if (!gpas->paspace_)
        return ZX_ERR_NO_MEMORY;
#if ARCH_ARM64
    gpas->paspace_->arch_aspace().arch_set_asid(vmid);
#endif

    // Initialize our VMAR with the provided VMO, mapped at address 0.
    fbl::RefPtr<VmMapping> mapping;
    zx_status_t status = gpas->paspace_->RootVmar()->CreateVmMapping(
        0 /* mapping_offset */, guest_phys_mem->size(), /* align_pow2*/ false, VMAR_FLAG_SPECIFIC,
        guest_phys_mem, /* vmo_offset */ 0, kMmuFlags, "guest_phys_mem_vmo", &mapping);
    if (status != ZX_OK)
        return status;

    *_gpas = fbl::move(gpas);
    return ZX_OK;
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

zx_status_t GuestPhysicalAddressSpace::MapInterruptController(vaddr_t guest_paddr,
                                                              paddr_t host_paddr, size_t size) {
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPhysical::Create(host_paddr, size, &vmo);
    if (status != ZX_OK)
        return status;

    status = vmo->SetMappingCachePolicy(ARCH_MMU_FLAG_CACHED);
    if (status != ZX_OK)
        return status;

    // The root VMAR will maintain a reference to the VmMapping internally so
    // we don't need to maintain a long-lived reference to the mapping here.
    fbl::RefPtr<VmMapping> mapping;
    status = paspace_->RootVmar()->CreateVmMapping(guest_paddr, vmo->size(), /* align_pow2*/ 0,
                                                   VMAR_FLAG_SPECIFIC, vmo, /* vmo_offset */ 0,
                                                   kInterruptMmuFlags, "guest_interrupt_vmo",
                                                   &mapping);
    if (status != ZX_OK)
        return status;

    // Write mapping to page table.
    status = mapping->MapRange(0, vmo->size(), true);
    if (status != ZX_OK) {
        mapping->Destroy();
        return status;
    }
    return ZX_OK;
}

zx_status_t GuestPhysicalAddressSpace::UnmapRange(vaddr_t guest_paddr, size_t size) {
    return paspace_->RootVmar()->Unmap(guest_paddr, size);
}

zx_status_t GuestPhysicalAddressSpace::GetPage(vaddr_t guest_paddr, paddr_t* host_paddr) {
    // Locate the VMO for the guest physical address (if present).
    AspaceVmoLocator vmo_locator(guest_paddr);
    paspace_->EnumerateChildren(&vmo_locator);
    fbl::RefPtr<VmObject> vmo = vmo_locator.vmo;
    if (!vmo)
        return ZX_ERR_NOT_FOUND;

    // Lookup the physical address of this page in the VMO.
    vaddr_t offset = guest_paddr - vmo_locator.base;
    return vmo->Lookup(offset, PAGE_SIZE, kPfFlags, guest_lookup_page, host_paddr);
}


zx_status_t GuestPhysicalAddressSpace::CreateGuestPtr(zx_vaddr_t guest_paddr, size_t size,
                                                      const char* name, GuestPtr* guest_ptr) {
    zx_vaddr_t begin = ROUNDDOWN(guest_paddr, PAGE_SIZE);
    zx_vaddr_t end = ROUNDUP(guest_paddr + size, PAGE_SIZE);
    // Overflow check.
    if (begin > end) {
        return ZX_ERR_INVALID_ARGS;
    }
    // Boundaries check.
    if (end > guest_phys_mem_->size()) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<VmMapping> mapping;
    zx_status_t status = VmAspace::kernel_aspace()->RootVmar()->CreateVmMapping(
        /* mapping_offset */ 0,
        end - begin,
        /* align_pow2 */ false,
        /* vmar_flags */ 0,
        guest_phys_mem_,
        begin,
        kGuestMmuFlags,
        name,
        &mapping);
    if (status != ZX_OK) {
        return status;
    }

    GuestPtr ptr(fbl::move(mapping), guest_paddr - begin);
    *guest_ptr = fbl::move(ptr);
    return ZX_OK;
}

} // namespace hypervisor
