// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/dispatcher.h>

class GuestDispatcher;
class Vcpu;
class VmObject;

class VcpuDispatcher final : public Dispatcher {
public:
    static mx_status_t Create(mxtl::RefPtr<GuestDispatcher> guest_dispatcher, mx_vaddr_t ip,
                              mx_vaddr_t cr3, mxtl::RefPtr<VmObject> apic_vmo,
                              mxtl::RefPtr<Dispatcher>* dispatcher, mx_rights_t* rights);
    ~VcpuDispatcher();

    mx_obj_type_t get_type() const { return MX_OBJ_TYPE_VCPU; }

    mx_status_t Resume(mx_guest_packet* packet);
    mx_status_t Interrupt(uint32_t vector);
    mx_status_t ReadState(uint32_t kind, void* buffer, uint32_t len) const;
    mx_status_t WriteState(uint32_t kind, const void* buffer, uint32_t len);

private:
    mxtl::Canary<mxtl::magic("VCPD")> canary_;
    mxtl::RefPtr<GuestDispatcher> guest_;
    mxtl::unique_ptr<Vcpu> vcpu_;

    explicit VcpuDispatcher(mxtl::RefPtr<GuestDispatcher> guest, mxtl::unique_ptr<Vcpu> vcpu);
};
