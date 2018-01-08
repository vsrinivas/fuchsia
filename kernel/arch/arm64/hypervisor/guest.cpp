// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/hypervisor.h>
#include <dev/interrupt/arm_gic_regs.h>
#include <hypervisor/guest_physical_address_space.h>
#include <vm/pmm.h>
#include <zircon/syscalls/hypervisor.h>

#include "el2_cpu_state_priv.h"

static const vaddr_t kGicvAddress = 0xe82b2000;
static const size_t kGicvSize = 0x2000;

static zx_status_t get_gicv(paddr_t* gicv_paddr) {
    // Check for presence of GICv2 virtualisation extensions.
    //
    // TODO(abdulla): Support GICv3 virtualisation.
    if (GICV_OFFSET == 0)
        return ZX_ERR_NOT_SUPPORTED;

    *gicv_paddr = vaddr_to_paddr(reinterpret_cast<void*>(GICV_ADDRESS));
    return ZX_OK;
}

// static
zx_status_t Guest::Create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* out) {
    uint8_t vmid;
    zx_status_t status = alloc_vmid(&vmid);
    if (status != ZX_OK)
        return status;
    // TODO(abdulla): Invalidate (TLBI + IC) after allocating VMID, in case it
    // was previously used.

    fbl::AllocChecker ac;
    fbl::unique_ptr<Guest> guest(new (&ac) Guest(vmid));
    if (!ac.check()) {
        free_vmid(vmid);
        return ZX_ERR_NO_MEMORY;
    }

    status = GuestPhysicalAddressSpace::Create(fbl::move(physmem), &guest->gpas_);
    if (status != ZX_OK)
        return status;

    paddr_t gicv_paddr;
    status = get_gicv(&gicv_paddr);
    if (status != ZX_OK)
        return status;
    status = guest->gpas_->MapInterruptController(kGicvAddress, gicv_paddr, kGicvSize);
    if (status != ZX_OK)
        return status;

    guest->gpas_->aspace()->arch_aspace().asid_ = vmid;
    *out = fbl::move(guest);
    return ZX_OK;
}

Guest::Guest(uint8_t vmid)
    : vmid_(vmid) {}

Guest::~Guest() {
    free_vmid(vmid_);
}

zx_status_t Guest::SetTrap(uint32_t kind, zx_vaddr_t addr, size_t len,
                           fbl::RefPtr<PortDispatcher> port, uint64_t key) {
    switch (kind) {
    case ZX_GUEST_TRAP_MEM:
        if (port)
            return ZX_ERR_INVALID_ARGS;
    /* fall-through */
    case ZX_GUEST_TRAP_BELL:
        break;
    case ZX_GUEST_TRAP_IO:
        return ZX_ERR_NOT_SUPPORTED;
    default:
        return ZX_ERR_INVALID_ARGS;
    }
    if (SIZE_MAX - len < addr)
        return ZX_ERR_OUT_OF_RANGE;
    if (!IS_PAGE_ALIGNED(addr) || !IS_PAGE_ALIGNED(len) || len == 0)
        return ZX_ERR_INVALID_ARGS;
    zx_status_t status = gpas_->UnmapRange(addr, len);
    if (status != ZX_OK)
        return status;
    return traps_.InsertTrap(kind, addr, len, fbl::move(port), key);
}

zx_status_t arch_guest_create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* guest) {
    if (arm64_get_boot_el() < 2)
        return ZX_ERR_NOT_SUPPORTED;
    return Guest::Create(fbl::move(physmem), guest);
}

zx_status_t arch_guest_set_trap(Guest* guest, uint32_t kind, zx_vaddr_t addr, size_t len,
                                fbl::RefPtr<PortDispatcher> port, uint64_t key) {
    return guest->SetTrap(kind, addr, len, port, key);
}
