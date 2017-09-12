// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/hypervisor.h>
#include <fbl/auto_call.h>
#include <zircon/errors.h>
#include <vm/vm_object.h>

#include "el2_cpu_state_priv.h"

// static
zx_status_t Guest::Create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* out) {
    uint8_t vmid;
    zx_status_t status = alloc_vmid(&vmid);
    if (status != ZX_OK)
        return status;
    auto auto_call = fbl::MakeAutoCall([=]() { free_vmid(vmid); });

    fbl::AllocChecker ac;
    fbl::unique_ptr<Guest> guest(new (&ac) Guest(vmid));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    auto_call.cancel();
    *out = fbl::move(guest);
    // TODO(abdulla): We intentionally return ZX_ERR_NOT_SUPPORTED, as the guest
    // physical address space has not been wired up yet.
    return ZX_ERR_NOT_SUPPORTED;
}

Guest::Guest(uint8_t vmid)
    : vmid_(vmid) {}

Guest::~Guest() {
    free_vmid(vmid_);
}

zx_status_t arch_guest_create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* guest) {
    if (arm64_get_boot_el() < 2)
        return ZX_ERR_NOT_SUPPORTED;

    return Guest::Create(fbl::move(physmem), guest);
}

zx_status_t arch_guest_set_trap(Guest* guest, uint32_t kind, zx_vaddr_t addr, size_t len,
                                fbl::RefPtr<PortDispatcher> port, uint64_t key) {
    return ZX_ERR_NOT_SUPPORTED;
}
