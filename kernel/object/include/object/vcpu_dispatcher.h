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

typedef struct mx_port_packet mx_port_packet_t;

class VcpuDispatcher final : public Dispatcher {
public:
    static mx_status_t Create(fbl::RefPtr<GuestDispatcher> guest_dispatcher, mx_vaddr_t ip,
                              mx_vaddr_t cr3, fbl::RefPtr<VmObject> apic_vmo,
                              fbl::RefPtr<Dispatcher>* dispatcher, mx_rights_t* rights);
    ~VcpuDispatcher();

    mx_obj_type_t get_type() const { return MX_OBJ_TYPE_VCPU; }

    mx_status_t Resume(mx_port_packet_t* packet);
    mx_status_t Interrupt(uint32_t vector);
    mx_status_t ReadState(uint32_t kind, void* buffer, uint32_t len) const;
    mx_status_t WriteState(uint32_t kind, const void* buffer, uint32_t len);

private:
    fbl::Canary<fbl::magic("VCPD")> canary_;
    fbl::RefPtr<GuestDispatcher> guest_;
    fbl::unique_ptr<Vcpu> vcpu_;

    explicit VcpuDispatcher(fbl::RefPtr<GuestDispatcher> guest, fbl::unique_ptr<Vcpu> vcpu);
};
