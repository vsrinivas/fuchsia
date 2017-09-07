// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/syscalls/hypervisor.h>
#include <magenta/types.h>
#include <object/port_dispatcher.h>

class Guest;
class VmObject;

class GuestDispatcher final : public Dispatcher {
public:
    static mx_status_t Create(fbl::RefPtr<VmObject> physmem,
                              fbl::RefPtr<Dispatcher>* dispatcher,
                              mx_rights_t* rights);
    ~GuestDispatcher();

    mx_obj_type_t get_type() const { return MX_OBJ_TYPE_GUEST; }
    Guest* guest() const { return guest_.get(); }

    mx_status_t SetTrap(uint32_t kind, mx_vaddr_t addr, size_t len,
                        fbl::RefPtr<PortDispatcher> port, uint64_t key);

private:
    fbl::Canary<fbl::magic("GSTD")> canary_;
    fbl::unique_ptr<Guest> guest_;

    explicit GuestDispatcher(fbl::unique_ptr<Guest> guest);
};
