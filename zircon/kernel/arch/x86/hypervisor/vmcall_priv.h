// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_HYPERVISOR_VMCALL_PRIV_H_
#define ZIRCON_KERNEL_ARCH_X86_HYPERVISOR_VMCALL_PRIV_H_

#include <zircon/syscalls/port.h>

struct GuestState;

// Dispatch a `syscall` that came through a `vmcall`.
zx_status_t vmcall_dispatch(GuestState& guest_state, uintptr_t& fs_base, zx_port_packet_t& packet);

#endif  // ZIRCON_KERNEL_ARCH_X86_HYPERVISOR_VMCALL_PRIV_H_
