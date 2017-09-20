// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/hypervisor.h>
#include <hypervisor/guest_physical_address_space.h>
#include <zircon/syscalls/hypervisor.h>

#include "el2_cpu_state_priv.h"

// static
zx_status_t Guest::Create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<Guest> guest(new (&ac) Guest);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    zx_status_t status = GuestPhysicalAddressSpace::Create(fbl::move(physmem), &guest->gpas_);
    if (status != ZX_OK)
        return status;

    status = alloc_vmid(&guest->vmid_);
    if (status != ZX_OK)
        return status;

    *out = fbl::move(guest);
    // TODO(abdulla): We intentionally return ZX_ERR_NOT_SUPPORTED, as the guest
    // physical address space has not been wired up yet.
    return ZX_ERR_NOT_SUPPORTED;
}

Guest::~Guest() {
    if (vmid_ != 0)
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
    return mux_.AddPortRange(kind, addr, len, fbl::move(port), key);
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
