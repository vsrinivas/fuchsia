// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/hypervisor.h>
#include <magenta/errors.h>

mx_status_t arch_vcpu_resume(Vcpu* vcpu, mx_port_packet_t* packet) {
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t arch_vcpu_interrupt(Vcpu* vcpu, uint32_t interrupt) {
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t arch_vcpu_read_state(const Vcpu* vcpu, uint32_t kind, void* buffer, uint32_t len) {
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t arch_vcpu_write_state(Vcpu* vcpu, uint32_t kind, const void* buffer, uint32_t len) {
    return MX_ERR_NOT_SUPPORTED;
}
