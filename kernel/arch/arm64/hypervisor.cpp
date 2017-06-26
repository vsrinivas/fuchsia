// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/hypervisor.h>
#include <magenta/errors.h>

status_t arch_hypervisor_create(mxtl::unique_ptr<HypervisorContext>* context) {
    return MX_ERR_NOT_SUPPORTED;
}

status_t arch_guest_create(HypervisorContext* hypervisor,
                           mxtl::RefPtr<VmObject> phys_mem,
                           mxtl::RefPtr<FifoDispatcher> ctl_fifo,
                           mxtl::unique_ptr<GuestContext>* context) {
    return MX_ERR_NOT_SUPPORTED;
}

status_t arch_guest_enter(const mxtl::unique_ptr<GuestContext>& context) {
    return MX_ERR_NOT_SUPPORTED;
}

status_t arch_guest_mem_trap(const mxtl::unique_ptr<GuestContext>& context, vaddr_t guest_paddr,
                             size_t size) {
    return MX_ERR_NOT_SUPPORTED;
}

status_t arch_guest_interrupt(const mxtl::unique_ptr<GuestContext>& context, uint8_t interrupt) {
    return MX_ERR_NOT_SUPPORTED;
}

status_t arch_guest_set_gpr(const mxtl::unique_ptr<GuestContext>& context,
                            const mx_guest_gpr_t& guest_gpr) {
    return MX_ERR_NOT_SUPPORTED;
}

status_t arch_guest_get_gpr(const mxtl::unique_ptr<GuestContext>& context,
                            mx_guest_gpr_t* guest_gpr) {
    return MX_ERR_NOT_SUPPORTED;
}

status_t arch_guest_set_ip(const mxtl::unique_ptr<GuestContext>& context, uintptr_t guest_ip) {
    return MX_ERR_NOT_SUPPORTED;
}
