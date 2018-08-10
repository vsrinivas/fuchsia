// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <hypervisor/guest_physical_address_space.h>

#include <arch/mmu.h>
#include <kernel/range_check.h>
#include <vm/arch_vm_aspace.h>
#include <vm/fault.h>
#include <vm/vm_object_physical.h>
#include <fbl/alloc_checker.h>

static constexpr uint kPfFlags = VMM_PF_FLAG_WRITE | VMM_PF_FLAG_SW_FAULT;

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
    explicit AspaceVmoLocator(vaddr_t address) : addr(address) {}

    bool OnVmMapping(const VmMapping* map, const VmAddressRegion* vmar, uint depth) final {
        if (addr < map->base() || addr >= (map->base() + map->size())) {
            // Mapping does not cover 'addr', return true to keep going.
            return true;
        }
        vmo = map->vmo();
        base = map->base();
        return false;
    }

    fbl::RefPtr<VmObject> vmo = nullptr;
    vaddr_t base = 0;
    const vaddr_t addr;
};

} // namespace

namespace hypervisor {

zx_status_t GuestPhysicalAddressSpace::Create(
#if ARCH_ARM64
                                              uint8_t vmid,
#endif
                                              fbl::unique_ptr<GuestPhysicalAddressSpace>* _gpas) {
    fbl::AllocChecker ac;
    auto gpas = fbl::make_unique_checked<GuestPhysicalAddressSpace>(&ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    gpas->guest_aspace_ = VmAspace::Create(VmAspace::TYPE_GUEST_PHYS, "guest_paspace");
    if (!gpas->guest_aspace_) {
        return ZX_ERR_NO_MEMORY;
    }
#if ARCH_ARM64
    gpas->arch_aspace()->arch_set_asid(vmid);
#endif
    *_gpas = fbl::move(gpas);
    return ZX_OK;
}

GuestPhysicalAddressSpace::~GuestPhysicalAddressSpace() {
    // VmAspace maintains a circular reference with it's root VMAR. We need to
    // destroy the VmAspace in order to break that reference and allow the
    // VmAspace to be destructed.
    if (guest_aspace_) {
        guest_aspace_->Destroy();
    }
}

zx_status_t GuestPhysicalAddressSpace::MapInterruptController(zx_gpaddr_t guest_paddr,
                                                              zx_paddr_t host_paddr, size_t len) {
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPhysical::Create(host_paddr, len, &vmo);
    if (status != ZX_OK) {
        return status;
    }

    status = vmo->SetMappingCachePolicy(ARCH_MMU_FLAG_CACHED);
    if (status != ZX_OK) {
        return status;
    }

    // The root VMAR will maintain a reference to the VmMapping internally so
    // we don't need to maintain a long-lived reference to the mapping here.
    fbl::RefPtr<VmMapping> mapping;
    status = RootVmar()->CreateVmMapping(guest_paddr, vmo->size(), /* align_pow2*/ 0,
                                         VMAR_FLAG_SPECIFIC, vmo, /* vmo_offset */ 0,
                                         kInterruptMmuFlags, "guest_interrupt_vmo",
                                         &mapping);
    if (status != ZX_OK) {
        return status;
    }

    // Write mapping to page table.
    status = mapping->MapRange(0, vmo->size(), true);
    if (status != ZX_OK) {
        mapping->Destroy();
        return status;
    }
    return ZX_OK;
}

zx_status_t GuestPhysicalAddressSpace::UnmapRange(zx_gpaddr_t guest_paddr, size_t len) {
    zx_status_t status = RootVmar()->Unmap(guest_paddr, len);
    if (status == ZX_ERR_INVALID_ARGS) {
        return ZX_OK;
    }
    return status;
}

zx_status_t GuestPhysicalAddressSpace::GetPage(zx_gpaddr_t guest_paddr, zx_paddr_t* host_paddr) {
    // Locate the VMO for the guest physical address (if present).
    AspaceVmoLocator vmo_locator(guest_paddr);
    guest_aspace_->EnumerateChildren(&vmo_locator);
    fbl::RefPtr<VmObject> vmo = vmo_locator.vmo;
    if (!vmo) {
        return ZX_ERR_NOT_FOUND;
    }

    // Lookup the physical address of this page in the VMO.
    zx_gpaddr_t offset = guest_paddr - vmo_locator.base;
    return vmo->Lookup(offset, PAGE_SIZE, kPfFlags, guest_lookup_page, host_paddr);
}

zx_status_t GuestPhysicalAddressSpace::PageFault(zx_gpaddr_t guest_paddr, uint flags) {
    return guest_aspace_->PageFault(guest_paddr, flags);
}

zx_status_t GuestPhysicalAddressSpace::CreateGuestPtr(zx_gpaddr_t guest_paddr, size_t len,
                                                      const char* name, GuestPtr* guest_ptr) {
    const zx_gpaddr_t begin = ROUNDDOWN(guest_paddr, PAGE_SIZE);
    const zx_gpaddr_t end = ROUNDUP(guest_paddr + len, PAGE_SIZE);
    const zx_gpaddr_t mapping_len = end - begin;
    if (begin > end || !InRange(begin, mapping_len, size())) {
        return ZX_ERR_INVALID_ARGS;
    }
    fbl::RefPtr<VmAddressRegionOrMapping> region = RootVmar()->FindRegion(begin);
    if (!region) {
        return ZX_ERR_NOT_FOUND;
    }
    fbl::RefPtr<VmMapping> guest_mapping = region->as_vm_mapping();
    if (!guest_mapping) {
        return ZX_ERR_WRONG_TYPE;
    }
    const uint64_t intra_mapping_offset = begin - guest_mapping->base();
    if (!InRange(intra_mapping_offset, mapping_len, guest_mapping->size())) {
        // The address range is not contained within a single mapping.
        return ZX_ERR_OUT_OF_RANGE;
    }

    fbl::RefPtr<VmMapping> host_mapping;
    zx_status_t status = VmAspace::kernel_aspace()->RootVmar()->CreateVmMapping(
        /* mapping_offset */ 0,
        mapping_len,
        /* align_pow2 */ false,
        /* vmar_flags */ 0,
        guest_mapping->vmo(),
        guest_mapping->object_offset() + intra_mapping_offset,
        kGuestMmuFlags,
        name,
        &host_mapping);
    if (status != ZX_OK) {
        return status;
    }

    *guest_ptr = GuestPtr(fbl::move(host_mapping), guest_paddr - begin);
    return ZX_OK;
}

} // namespace hypervisor
