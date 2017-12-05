// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/syscalls/hypervisor.h>
#include <zircon/types.h>
#include <object/port_dispatcher.h>

class Guest;
class VmObject;

class GuestDispatcher final : public SoloDispatcher {
public:
    static zx_status_t Create(fbl::RefPtr<VmObject> physmem,
                              fbl::RefPtr<Dispatcher>* dispatcher,
                              zx_rights_t* rights);
    ~GuestDispatcher();

    zx_obj_type_t get_type() const { return ZX_OBJ_TYPE_GUEST; }
    Guest* guest() const { return guest_.get(); }

    zx_status_t SetTrap(uint32_t kind, zx_vaddr_t addr, size_t len,
                        fbl::RefPtr<PortDispatcher> port, uint64_t key);

private:
    fbl::Canary<fbl::magic("GSTD")> canary_;
    fbl::unique_ptr<Guest> guest_;

    explicit GuestDispatcher(fbl::unique_ptr<Guest> guest);
};
