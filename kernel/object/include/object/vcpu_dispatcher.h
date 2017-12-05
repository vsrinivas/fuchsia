// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <object/dispatcher.h>

class GuestDispatcher;
class Vcpu;
class VmObject;

typedef struct zx_port_packet zx_port_packet_t;

class VcpuDispatcher final : public SoloDispatcher {
public:
    static zx_status_t Create(fbl::RefPtr<GuestDispatcher> guest_dispatcher, zx_vaddr_t entry,
                              fbl::RefPtr<Dispatcher>* dispatcher, zx_rights_t* rights);
    ~VcpuDispatcher();

    zx_obj_type_t get_type() const { return ZX_OBJ_TYPE_VCPU; }

    zx_status_t Resume(zx_port_packet_t* packet);
    zx_status_t Interrupt(uint32_t vector);
    zx_status_t ReadState(uint32_t kind, void* buffer, uint32_t len) const;
    zx_status_t WriteState(uint32_t kind, const void* buffer, uint32_t len);

private:
    fbl::Canary<fbl::magic("VCPD")> canary_;
    fbl::RefPtr<GuestDispatcher> guest_;
    fbl::unique_ptr<Vcpu> vcpu_;

    explicit VcpuDispatcher(fbl::RefPtr<GuestDispatcher> guest, fbl::unique_ptr<Vcpu> vcpu);
};
