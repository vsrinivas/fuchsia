// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <object/handle.h>
#include <object/port_dispatcher.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/types.h>

class Guest;
class VmObject;

class GuestDispatcher final : public SoloDispatcher<GuestDispatcher, ZX_DEFAULT_GUEST_RIGHTS> {
public:
    static zx_status_t Create(KernelHandle<GuestDispatcher>* guest_handle,
                              zx_rights_t* guest_rights,
                              KernelHandle<VmAddressRegionDispatcher>* vmar_handle,
                              zx_rights_t* vmar_rights);
    ~GuestDispatcher();

    zx_obj_type_t get_type() const { return ZX_OBJ_TYPE_GUEST; }
    Guest* guest() const { return guest_.get(); }

    zx_status_t SetTrap(uint32_t kind, zx_vaddr_t addr, size_t len,
                        fbl::RefPtr<PortDispatcher> port, uint64_t key);

private:
    ktl::unique_ptr<Guest> guest_;

    explicit GuestDispatcher(ktl::unique_ptr<Guest> guest);
};
