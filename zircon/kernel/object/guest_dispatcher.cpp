// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/guest_dispatcher.h>

#include <arch/hypervisor.h>
#include <fbl/alloc_checker.h>
#include <lib/counters.h>
#include <object/vm_address_region_dispatcher.h>
#include <zircon/rights.h>

KCOUNTER(dispatcher_guest_create_count, "dispatcher.guest.create")
KCOUNTER(dispatcher_guest_destroy_count, "dispatcher.guest.destroy")

// static
zx_status_t GuestDispatcher::Create(KernelHandle<GuestDispatcher>* guest_handle,
                                    zx_rights_t* guest_rights,
                                    KernelHandle<VmAddressRegionDispatcher>* vmar_handle,
                                    zx_rights_t* vmar_rights) {
    ktl::unique_ptr<Guest> guest;
    zx_status_t status = Guest::Create(&guest);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    KernelHandle new_guest_handle(fbl::AdoptRef(new (&ac) GuestDispatcher(ktl::move(guest))));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    status = VmAddressRegionDispatcher::Create(
        new_guest_handle.dispatcher()->guest()->AddressSpace()->RootVmar(), 0, vmar_handle,
        vmar_rights);
    if (status != ZX_OK) {
        return status;
    }

    *guest_rights = default_rights();
    *guest_handle = ktl::move(new_guest_handle);
    return ZX_OK;
}

GuestDispatcher::GuestDispatcher(ktl::unique_ptr<Guest> guest)
    : guest_(ktl::move(guest)) {
    kcounter_add(dispatcher_guest_create_count, 1);
}

GuestDispatcher::~GuestDispatcher() {
    kcounter_add(dispatcher_guest_destroy_count, 1);
}

zx_status_t GuestDispatcher::SetTrap(uint32_t kind, zx_vaddr_t addr, size_t len,
                                     fbl::RefPtr<PortDispatcher> port, uint64_t key) {
    canary_.Assert();
    return guest_->SetTrap(kind, addr, len, ktl::move(port), key);
}
