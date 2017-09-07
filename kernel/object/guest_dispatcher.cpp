// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/guest_dispatcher.h>

#include <arch/hypervisor.h>
#include <vm/vm_object.h>
#include <magenta/rights.h>
#include <fbl/alloc_checker.h>

// static
mx_status_t GuestDispatcher::Create(fbl::RefPtr<VmObject> physmem,
                                    fbl::RefPtr<Dispatcher>* dispatcher,
                                    mx_rights_t* rights) {
    fbl::unique_ptr<Guest> guest;
    mx_status_t status = arch_guest_create(physmem, &guest);
    if (status != MX_OK)
        return status;

    fbl::AllocChecker ac;
    auto disp = new (&ac) GuestDispatcher(fbl::move(guest));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    *rights = MX_DEFAULT_GUEST_RIGHTS;
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return MX_OK;
}

GuestDispatcher::GuestDispatcher(fbl::unique_ptr<Guest> guest)
    : guest_(fbl::move(guest)) {}

GuestDispatcher::~GuestDispatcher() {}

mx_status_t GuestDispatcher::SetTrap(uint32_t kind, mx_vaddr_t addr, size_t len,
                                     fbl::RefPtr<PortDispatcher> port, uint64_t key) {
    canary_.Assert();

    return arch_guest_set_trap(guest_.get(), kind, addr, len, fbl::move(port), key);
}
