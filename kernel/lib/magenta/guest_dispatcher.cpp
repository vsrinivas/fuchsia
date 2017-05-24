// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/vm/vm_object.h>
#include <magenta/fifo_dispatcher.h>
#include <magenta/guest_dispatcher.h>
#include <magenta/hypervisor_dispatcher.h>
#include <mxalloc/new.h>

constexpr mx_rights_t kDefaultGuestRights = MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_EXECUTE;

// static
mx_status_t GuestDispatcher::Create(mxtl::RefPtr<HypervisorDispatcher> hypervisor,
                                    mxtl::RefPtr<VmObject> phys_mem,
                                    mxtl::RefPtr<FifoDispatcher> ctl_fifo,
                                    mxtl::RefPtr<Dispatcher>* dispatcher,
                                    mx_rights_t* rights) {
    mxtl::unique_ptr<GuestContext> context;
    mx_status_t status = arch_guest_create(phys_mem, ctl_fifo, &context);
    if (status != NO_ERROR)
        return status;

    AllocChecker ac;
    auto guest = mxtl::AdoptRef(new (&ac) GuestDispatcher(hypervisor, mxtl::move(context)));
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultGuestRights;
    *dispatcher = mxtl::RefPtr<Dispatcher>(guest.get());
    return NO_ERROR;
}

GuestDispatcher::GuestDispatcher(mxtl::RefPtr<HypervisorDispatcher> hypervisor,
                                 mxtl::unique_ptr<GuestContext> context)
    : hypervisor_(hypervisor), context_(mxtl::move(context)) {}

GuestDispatcher::~GuestDispatcher() {}

mx_status_t GuestDispatcher::Enter() {
    canary_.Assert();

    return arch_guest_enter(context_);
}

mx_status_t GuestDispatcher::MemTrap(mx_vaddr_t guest_paddr, size_t size) {
    canary_.Assert();

    return arch_guest_mem_trap(context_, guest_paddr, size);
}

mx_status_t GuestDispatcher::SetGpr(const mx_guest_gpr_t& guest_gpr) {
    canary_.Assert();

    return arch_guest_set_gpr(context_, guest_gpr);
}

mx_status_t GuestDispatcher::GetGpr(mx_guest_gpr_t* guest_gpr) const {
    canary_.Assert();

    return arch_guest_get_gpr(context_, guest_gpr);
}

#if ARCH_X86_64
mx_status_t GuestDispatcher::SetApicMem(mxtl::RefPtr<VmObject> apic_mem) {
    canary_.Assert();

    return x86_guest_set_apic_mem(context_, apic_mem);
}
#endif // ARCH_X86_64

mx_status_t GuestDispatcher::set_ip(uintptr_t guest_ip) {
    canary_.Assert();

    return arch_guest_set_ip(context_, guest_ip);
}

#if ARCH_X86_64
mx_status_t GuestDispatcher::set_cr3(uintptr_t guest_cr3) {
    canary_.Assert();

    return x86_guest_set_cr3(context_, guest_cr3);
}
#endif // ARCH_X86_64
