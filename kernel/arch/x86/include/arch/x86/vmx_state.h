// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>

#define VS_RESUME   0

#define HS_RIP      (VS_RESUME + 8)
#define HS_RBX      (HS_RIP + 8)
#define HS_RSP      (HS_RBX + 8)
#define HS_RBP      (HS_RSP + 8)
#define HS_R12      (HS_RBP + 8)
#define HS_R13      (HS_R12 + 8)
#define HS_R14      (HS_R13 + 8)
#define HS_R15      (HS_R14 + 8)
#define HS_RFLAGS   (HS_R15 + 8)

#define GS_RAX      (HS_RFLAGS + 16)
#define GS_RCX      (GS_RAX + 8)
#define GS_RDX      (GS_RCX + 8)
#define GS_RBX      (GS_RDX + 8)
#define GS_RBP      (GS_RBX + 8)
#define GS_RSI      (GS_RBP + 8)
#define GS_RDI      (GS_RSI + 8)
#define GS_R8       (GS_RDI + 8)
#define GS_R9       (GS_R8 + 8)
#define GS_R10      (GS_R9 + 8)
#define GS_R11      (GS_R10 + 8)
#define GS_R12      (GS_R11 + 8)
#define GS_R13      (GS_R12 + 8)
#define GS_R14      (GS_R13 + 8)
#define GS_R15      (GS_R14 + 8)
#define GS_CR2      (GS_R15 + 8)

#ifndef ASSEMBLY

#include <sys/types.h>

/* Holds the register state used to restore a host. */
struct HostState {
    // Return address.
    uint64_t rip;

    // Callee-save registers.
    uint64_t rbx;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    // Processor flags.
    uint64_t rflags;

    // Extended control registers.
    uint64_t xcr0;
};

struct GuestState {
    //  RIP, RSP, and RFLAGS are automatically saved by VMX in the VMCS.
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    // Control registers.
    uint64_t cr2;

    // Extended control registers.
    uint64_t xcr0;
};

struct VmxState {
    bool resume;
    HostState host_state;
    GuestState guest_state;
};

static_assert(__offsetof(VmxState, resume) == VS_RESUME, "");

static_assert(__offsetof(VmxState, host_state.rip) == HS_RIP, "");
static_assert(__offsetof(VmxState, host_state.rsp) == HS_RSP, "");
static_assert(__offsetof(VmxState, host_state.rbp) == HS_RBP, "");
static_assert(__offsetof(VmxState, host_state.rbx) == HS_RBX, "");
static_assert(__offsetof(VmxState, host_state.r12) == HS_R12, "");
static_assert(__offsetof(VmxState, host_state.r13) == HS_R13, "");
static_assert(__offsetof(VmxState, host_state.r14) == HS_R14, "");
static_assert(__offsetof(VmxState, host_state.r15) == HS_R15, "");
static_assert(__offsetof(VmxState, host_state.rflags) == HS_RFLAGS, "");

static_assert(__offsetof(VmxState, guest_state.rax) == GS_RAX, "");
static_assert(__offsetof(VmxState, guest_state.rbx) == GS_RBX, "");
static_assert(__offsetof(VmxState, guest_state.rcx) == GS_RCX, "");
static_assert(__offsetof(VmxState, guest_state.rdx) == GS_RDX, "");
static_assert(__offsetof(VmxState, guest_state.rdi) == GS_RDI, "");
static_assert(__offsetof(VmxState, guest_state.rsi) == GS_RSI, "");
static_assert(__offsetof(VmxState, guest_state.rbp) == GS_RBP, "");
static_assert(__offsetof(VmxState, guest_state.r8) == GS_R8, "");
static_assert(__offsetof(VmxState, guest_state.r9) == GS_R9, "");
static_assert(__offsetof(VmxState, guest_state.r10) == GS_R10, "");
static_assert(__offsetof(VmxState, guest_state.r11) == GS_R11, "");
static_assert(__offsetof(VmxState, guest_state.r12) == GS_R12, "");
static_assert(__offsetof(VmxState, guest_state.r13) == GS_R13, "");
static_assert(__offsetof(VmxState, guest_state.r14) == GS_R14, "");
static_assert(__offsetof(VmxState, guest_state.r15) == GS_R15, "");
static_assert(__offsetof(VmxState, guest_state.cr2) == GS_CR2, "");

__BEGIN_CDECLS
/* Launch the guest and save the host state.
 * If we return 0, we have exited from the guest, otherwise we have failed to
 * launch the guest.
 */
status_t vmx_enter(VmxState* vmx_state);

/* Exit from the guest, and load the saved host state.
 * This function is never called directly, but is executed on exit from a guest.
 * It calls vmx_exit before returning through vmx_enter.
 */
void vmx_exit_entry();
void vmx_exit(VmxState* vmx_state);

__END_CDECLS

#endif // ASSEMBLY
