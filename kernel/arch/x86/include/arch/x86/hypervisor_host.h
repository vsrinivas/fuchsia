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
/* Save the host state.
 * This is the VMX equivalent of setjmp. If we return 0 we have saved the host
 * state, if we return 1 we have loaded the host state.
 */
int vmx_host_save(VmxHostState* host_state);

/* Load the host state.
 * This is the VMX equivalent of longjmp. This is never called directly by the
 * code, but is executed by VMX on VM exit.  It calls vmx_host_load() before
 * returning through vmx_host_save.
 */
void vmx_host_load_entry();
void vmx_host_load(VmxHostState* host_state);

__END_CDECLS

#endif // ASSEMBLY
