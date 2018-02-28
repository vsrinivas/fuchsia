// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/hypervisor.h>
#include <dev/interrupt/arm_gic_hw_interface.h>
#include <hypervisor/guest_physical_address_space.h>
#include <vm/pmm.h>
#include <zircon/syscalls/hypervisor.h>

#include "el2_cpu_state_priv.h"

static const vaddr_t kGicvAddress = 0xe82b2000;
static const size_t kGicvSize = 0x2000;

// static
zx_status_t Guest::Create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* out) {
    if (arm64_get_boot_el() < 2)
        return ZX_ERR_NOT_SUPPORTED;

    uint8_t vmid;
    zx_status_t status = alloc_vmid(&vmid);
    if (status != ZX_OK)
        return status;

    fbl::AllocChecker ac;
    fbl::unique_ptr<Guest> guest(new (&ac) Guest(vmid));
    if (!ac.check()) {
        free_vmid(vmid);
        return ZX_ERR_NO_MEMORY;
    }

    fbl::AutoLock lock(&guest->vcpu_mutex_);
    status = guest->vpid_allocator_.Init();
    if (status != ZX_OK)
        return status;

    status = hypervisor::GuestPhysicalAddressSpace::Create(fbl::move(physmem), vmid, &guest->gpas_);
    if (status != ZX_OK)
        return status;

    paddr_t gicv_paddr;
    status = gic_get_gicv(&gicv_paddr);

    // If status == ZX_ERR_NOT_FOUND, we are running GICv3
    // There is no need to map GICV to the guest
    // Handle other cases below
    if (status == ZX_OK) {
        status = guest->gpas_->MapInterruptController(kGicvAddress, gicv_paddr, kGicvSize);
        if (status != ZX_OK) {
            return status;
        }
    } else if (status == ZX_ERR_NOT_SUPPORTED) {
        return status;
    }

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
        break;
    case ZX_GUEST_TRAP_BELL:
        if (!port)
            return ZX_ERR_INVALID_ARGS;
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

zx_status_t Guest::AllocVpid(uint8_t* vpid) {
    fbl::AutoLock lock(&vcpu_mutex_);
    return vpid_allocator_.AllocId(vpid);
}

zx_status_t Guest::FreeVpid(uint8_t vpid) {
    fbl::AutoLock lock(&vcpu_mutex_);
    return vpid_allocator_.FreeId(vpid);
}
