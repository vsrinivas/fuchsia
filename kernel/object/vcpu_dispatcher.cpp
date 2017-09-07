// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/vcpu_dispatcher.h>

#include <arch/hypervisor.h>
#include <hypervisor/guest_physical_address_space.h>
#include <vm/vm_object.h>
#include <magenta/rights.h>
#include <magenta/types.h>
#include <fbl/alloc_checker.h>
#include <object/guest_dispatcher.h>

mx_status_t VcpuDispatcher::Create(fbl::RefPtr<GuestDispatcher> guest_dispatcher, mx_vaddr_t ip,
                                   mx_vaddr_t cr3, fbl::RefPtr<VmObject> apic_vmo,
                                   fbl::RefPtr<Dispatcher>* dispatcher, mx_rights_t* rights) {
    Guest* guest = guest_dispatcher->guest();
    GuestPhysicalAddressSpace* gpas = guest->AddressSpace();
    if (ip >= gpas->size())
        return MX_ERR_INVALID_ARGS;
    if (cr3 >= gpas->size() - PAGE_SIZE)
        return MX_ERR_INVALID_ARGS;

    fbl::unique_ptr<Vcpu> vcpu;
#if ARCH_X86_64
    mx_status_t status = x86_vcpu_create(ip, cr3, apic_vmo, guest->ApicAccessAddress(),
                                      guest->MsrBitmapsAddress(), gpas, guest->Mux(), &vcpu);
#else // ARCH_X86_64
    mx_status_t status = MX_ERR_NOT_SUPPORTED;
#endif
    if (status != MX_OK)
        return status;

    fbl::AllocChecker ac;
    auto disp = new (&ac) VcpuDispatcher(guest_dispatcher, fbl::move(vcpu));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    *rights = MX_DEFAULT_VCPU_RIGHTS;
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return MX_OK;
}

VcpuDispatcher::VcpuDispatcher(fbl::RefPtr<GuestDispatcher> guest, fbl::unique_ptr<Vcpu> vcpu)
    : guest_(guest), vcpu_(fbl::move(vcpu)) {}

VcpuDispatcher::~VcpuDispatcher() {}

mx_status_t VcpuDispatcher::Resume(mx_port_packet_t* packet) {
    canary_.Assert();

    return arch_vcpu_resume(vcpu_.get(), packet);
}

mx_status_t VcpuDispatcher::Interrupt(uint32_t vector) {
    canary_.Assert();

    return arch_vcpu_interrupt(vcpu_.get(), vector);
}

mx_status_t VcpuDispatcher::ReadState(uint32_t kind, void* buffer, uint32_t len) const {
    canary_.Assert();

    return arch_vcpu_read_state(vcpu_.get(), kind, buffer, len);
}

mx_status_t VcpuDispatcher::WriteState(uint32_t kind, const void* buffer, uint32_t len) {
    canary_.Assert();

    return arch_vcpu_write_state(vcpu_.get(), kind, buffer, len);
}
