// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <object/dispatcher.h>
#include <object/handle.h>
#include <zircon/rights.h>

class GuestDispatcher;
class Vcpu;
class VmObject;

typedef struct zx_port_packet zx_port_packet_t;

class VcpuDispatcher final : public SoloDispatcher<VcpuDispatcher, ZX_DEFAULT_VCPU_RIGHTS> {
public:
    static zx_status_t Create(fbl::RefPtr<GuestDispatcher> guest_dispatcher, zx_vaddr_t entry,
                              KernelHandle<VcpuDispatcher>* handle, zx_rights_t* rights);
    ~VcpuDispatcher();

    zx_obj_type_t get_type() const { return ZX_OBJ_TYPE_VCPU; }
    const fbl::RefPtr<GuestDispatcher>& guest() const { return guest_; }

    zx_status_t Resume(zx_port_packet_t* packet);
    // Adds an interrupt vector to the list of pending interrupts. If the VCPU
    // is running, this returns a CPU mask that can be used to interrupt it.
    cpu_mask_t PhysicalInterrupt(uint32_t vector);
    void VirtualInterrupt(uint32_t vector);
    zx_status_t ReadState(uint32_t kind, void* buffer, size_t len) const;
    zx_status_t WriteState(uint32_t kind, const void* buffer, size_t len);

private:
    fbl::RefPtr<GuestDispatcher> guest_;
    ktl::unique_ptr<Vcpu> vcpu_;

    explicit VcpuDispatcher(fbl::RefPtr<GuestDispatcher> guest, ktl::unique_ptr<Vcpu> vcpu);
};
