// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>

#define VHS_RIP     0
#define VHS_RSP     8
#define VHS_RBP     16
#define VHS_RBX     24
#define VHS_R12     32
#define VHS_R13     40
#define VHS_R14     48
#define VHS_R15     56
#define VHS_RFLAGS  64

#ifndef ASSEMBLY

/* Holds the register state used to restore a host. */
struct VmxHostState {
    // Return address.
    uint64_t rip;

    // Callee-save registers.
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    // Processor flags.
    uint64_t rflags;
};

static_assert(__offsetof(VmxHostState, rip) == VHS_RIP, "");
static_assert(__offsetof(VmxHostState, rsp) == VHS_RSP, "");
static_assert(__offsetof(VmxHostState, rbp) == VHS_RBP, "");
static_assert(__offsetof(VmxHostState, rbx) == VHS_RBX, "");
static_assert(__offsetof(VmxHostState, r12) == VHS_R12, "");
static_assert(__offsetof(VmxHostState, r13) == VHS_R13, "");
static_assert(__offsetof(VmxHostState, r14) == VHS_R14, "");
static_assert(__offsetof(VmxHostState, r15) == VHS_R15, "");
static_assert(__offsetof(VmxHostState, rflags) == VHS_RFLAGS, "");

__BEGIN_CDECLS
/* Launch the guest and save the host state.
 * If we return 0, we have exited from the guest, otherwise we have failed to
 * launch the guest.
 */
int vmx_launch(VmxHostState* host_state);

/* Exit from the guest, and load the saved host state.
 * This function is never called directly, but is executed on exit from a guest.
 * It calls vmx_exit before returning through vmx_launch.
 */
void vmx_exit_entry();
void vmx_exit(VmxHostState* host_state);

__END_CDECLS

#endif // ASSEMBLY
