// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/vcpu_dispatcher.h>

#include <arch/hypervisor.h>
#include <fbl/alloc_checker.h>
#include <hypervisor/guest_physical_address_space.h>
#include <object/guest_dispatcher.h>
#include <vm/vm_object.h>
#include <zircon/rights.h>
#include <zircon/types.h>

zx_status_t VcpuDispatcher::Create(fbl::RefPtr<GuestDispatcher> guest_dispatcher, zx_vaddr_t entry,
                                   fbl::RefPtr<Dispatcher>* dispatcher, zx_rights_t* rights) {
    Guest* guest = guest_dispatcher->guest();

    fbl::unique_ptr<Vcpu> vcpu;
    zx_status_t status = Vcpu::Create(guest, entry, &vcpu);
    if (status != ZX_OK)
        return status;

    fbl::AllocChecker ac;
    auto disp = new (&ac) VcpuDispatcher(guest_dispatcher, fbl::move(vcpu));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    *rights = ZX_DEFAULT_VCPU_RIGHTS;
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

VcpuDispatcher::VcpuDispatcher(fbl::RefPtr<GuestDispatcher> guest, fbl::unique_ptr<Vcpu> vcpu)
    : guest_(guest), vcpu_(fbl::move(vcpu)) {}

VcpuDispatcher::~VcpuDispatcher() {}

zx_status_t VcpuDispatcher::Resume(zx_port_packet_t* packet) {
    canary_.Assert();
    return vcpu_->Resume(packet);
}

zx_status_t VcpuDispatcher::Interrupt(uint32_t vector) {
    canary_.Assert();
    return vcpu_->Interrupt(vector);
}

zx_status_t VcpuDispatcher::ReadState(uint32_t kind, void* buffer, uint32_t len) const {
    canary_.Assert();
    return vcpu_->ReadState(kind, buffer, len);
}

zx_status_t VcpuDispatcher::WriteState(uint32_t kind, const void* buffer, uint32_t len) {
    canary_.Assert();
    return vcpu_->WriteState(kind, buffer, len);
}
