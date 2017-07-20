// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/hypervisor.h>
#include <magenta/errors.h>

status_t arch_guest_create(mxtl::RefPtr<VmObject> physmem, mxtl::unique_ptr<Guest>* guest) {
    return MX_ERR_NOT_SUPPORTED;
}

status_t arch_guest_set_trap(Guest* guest, mx_trap_address_space_t aspace, mx_vaddr_t addr,
                             size_t len, mxtl::RefPtr<FifoDispatcher> fifo) {
    return MX_ERR_NOT_SUPPORTED;
}

status_t arch_vcpu_resume(Vcpu* vcpu, mx_guest_packet_t* packet) {
    return MX_ERR_NOT_SUPPORTED;
}

status_t arch_vcpu_interrupt(Vcpu* vcpu, uint32_t interrupt) {
    return MX_ERR_NOT_SUPPORTED;
}

status_t arch_vcpu_read_state(const Vcpu* vcpu, mx_vcpu_state_t* state) {
    return MX_ERR_NOT_SUPPORTED;
}

status_t arch_vcpu_write_state(Vcpu* vcpu, const mx_vcpu_state_t& state) {
    return MX_ERR_NOT_SUPPORTED;
}
