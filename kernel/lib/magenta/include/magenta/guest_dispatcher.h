// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/hypervisor_dispatcher.h>
#include <mxtl/canary.h>

class GuestDispatcher final : public Dispatcher {
public:
    static mx_status_t Create(mxtl::RefPtr<HypervisorDispatcher> hypervisor,
                              mxtl::RefPtr<VmObject> phys_mem,
                              mxtl::RefPtr<FifoDispatcher> ctl_fifo,
                              mxtl::RefPtr<Dispatcher>* dispatcher,
                              mx_rights_t* rights);

    ~GuestDispatcher();

    mx_obj_type_t get_type() const { return MX_OBJ_TYPE_GUEST; }

    mx_status_t Enter();
    mx_status_t MemTrap(mx_vaddr_t guest_paddr, size_t size);
    mx_status_t SetGpr(const mx_guest_gpr_t& guest_gpr);
    mx_status_t GetGpr(mx_guest_gpr_t* guest_gpr) const;
#if ARCH_X86_64
    mx_status_t SetApicMem(mxtl::RefPtr<VmObject> apic_mem);
#endif // ARCH_X86_64

    mx_status_t set_ip(uintptr_t guest_ip);
#if ARCH_X86_64
    mx_status_t set_cr3(uintptr_t guest_cr3);
#endif // ARCH_X86_64

private:
    mxtl::Canary<mxtl::magic("GSTD")> canary_;
    mxtl::RefPtr<HypervisorDispatcher> hypervisor_;
    mxtl::unique_ptr<GuestContext> context_;

    explicit GuestDispatcher(mxtl::RefPtr<HypervisorDispatcher> hypervisor,
                             mxtl::unique_ptr<GuestContext> context);
};
