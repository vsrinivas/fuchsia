// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/hypervisor.h>
#include <hypervisor/guest_physical_address_space.h>
#include <kernel/vm/vm_object.h>
#include <magenta/guest_dispatcher.h>
#include <magenta/rights.h>
#include <magenta/types.h>
#include <magenta/vcpu_dispatcher.h>
#include <mxalloc/new.h>

status_t VcpuDispatcher::Create(mxtl::RefPtr<GuestDispatcher> guest_dispatcher, mx_vaddr_t ip,
                                mx_vaddr_t cr3, mxtl::RefPtr<VmObject> apic_vmo,
                                mxtl::RefPtr<Dispatcher>* dispatcher, mx_rights_t* rights) {
    Guest* guest = guest_dispatcher->guest();
    GuestPhysicalAddressSpace* gpas = guest->AddressSpace();
    if (ip >= gpas->size())
        return MX_ERR_INVALID_ARGS;
    if (cr3 >= gpas->size() - PAGE_SIZE)
        return MX_ERR_INVALID_ARGS;

    mxtl::unique_ptr<Vcpu> vcpu;
#if ARCH_X86_64
    status_t status = x86_vcpu_create(ip, cr3, apic_vmo, guest->ApicAccessAddress(),
                                      guest->MsrBitmapsAddress(), gpas, guest->Mux(), &vcpu);
#else // ARCH_X86_64
    status_t status = MX_ERR_NOT_SUPPORTED;
#endif
    if (status != MX_OK)
        return status;

    AllocChecker ac;
    auto disp = new (&ac) VcpuDispatcher(guest_dispatcher, mxtl::move(vcpu));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    *rights = MX_DEFAULT_VCPU_RIGHTS;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    return MX_OK;
}

VcpuDispatcher::VcpuDispatcher(mxtl::RefPtr<GuestDispatcher> guest, mxtl::unique_ptr<Vcpu> vcpu)
    : guest_(guest), vcpu_(mxtl::move(vcpu)) {}

VcpuDispatcher::~VcpuDispatcher() {}

mx_status_t VcpuDispatcher::Resume(mx_guest_packet* packet) {
    canary_.Assert();

    return arch_vcpu_resume(vcpu_.get(), packet);
}

mx_status_t VcpuDispatcher::Interrupt(uint32_t vector) {
    canary_.Assert();

    return arch_vcpu_interrupt(vcpu_.get(), vector);
}

mx_status_t VcpuDispatcher::ReadState(mx_vcpu_state_t* vcpu_state) const {
    canary_.Assert();

    return arch_vcpu_read_state(vcpu_.get(), vcpu_state);
}

mx_status_t VcpuDispatcher::WriteState(const mx_vcpu_state_t& vcpu_state) {
    canary_.Assert();

    return arch_vcpu_write_state(vcpu_.get(), vcpu_state);
}
