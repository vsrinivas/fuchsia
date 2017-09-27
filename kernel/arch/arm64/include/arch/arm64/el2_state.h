// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/compiler.h>

#define SS_CNTKCTL_EL1      0
#define SS_CONTEXTIDR_EL1   (SS_CNTKCTL_EL1 + 4)
#define SS_CPACR_EL1        (SS_CONTEXTIDR_EL1 + 4)
#define SS_CSSELR_EL1       (SS_CPACR_EL1 + 4)
#define SS_ESR_EL1          (SS_CSSELR_EL1 + 4)
#define SS_FAR_EL1          (SS_ESR_EL1 + 8)
#define SS_MAIR_EL1         (SS_FAR_EL1 + 8)
#define SS_MDSCR_EL1        (SS_MAIR_EL1 + 8)
#define SS_SCTLR_EL1        (SS_MDSCR_EL1 + 4)
#define SS_SP_EL1           (SS_SCTLR_EL1 + 4)
#define SS_TPIDR_EL1        (SS_SP_EL1 + 8)
#define SS_TCR_EL1          (SS_TPIDR_EL1 + 8)
#define SS_TTBR0_EL1        (SS_TCR_EL1 + 8)
#define SS_TTBR1_EL1        (SS_TTBR0_EL1 + 8)
#define SS_VBAR_EL1         (SS_TTBR1_EL1 + 8)
#define SS_ELR_EL2          (SS_VBAR_EL1 + 8)
#define SS_SPSR_EL2         (SS_ELR_EL2 + 8)
#define SS_VMPIDR_EL2       (SS_SPSR_EL2 + 8)

#define ES_ESR_EL2          0

#define HS_R19              (ES_ESR_EL2 + 8)
#define HS_SYSTEM_STATE     (HS_R19 + (10 * 8))

#define GS_R0               (HS_SYSTEM_STATE + SS_VMPIDR_EL2 + 8)
#define GS_SYSTEM_STATE     (GS_R0 + (31 * 8))

#ifndef ASSEMBLY

#include <zircon/types.h>

struct SystemState {
    uint32_t cntkctl_el1;
    uint32_t contextidr_el1;
    uint32_t cpacr_el1;
    uint32_t csselr_el1;
    uint32_t esr_el1;
    uint64_t far_el1;
    uint64_t mair_el1;
    uint32_t mdscr_el1;
    uint32_t sctlr_el1;
    uint64_t sp_el1;
    uint64_t tpidr_el1;
    uint64_t tcr_el1;
    uint64_t ttbr0_el1;
    uint64_t ttbr1_el1;
    uint64_t vbar_el1;

    uint64_t elr_el2;
    uint64_t spsr_el2;
    uint64_t vmpidr_el2;
};

struct HostState {
    // Callee-save registers (X19 to X28).
    uint64_t r[10];
    SystemState system_state;
};

struct GuestState {
    uint64_t r[31];
    SystemState system_state;
};

struct El2State {
    uint32_t esr_el2;
    HostState host_state;
    GuestState guest_state;
};

static_assert(__offsetof(SystemState, cntkctl_el1) == SS_CNTKCTL_EL1, "");
static_assert(__offsetof(SystemState, contextidr_el1) == SS_CONTEXTIDR_EL1, "");
static_assert(__offsetof(SystemState, cpacr_el1) == SS_CPACR_EL1, "");
static_assert(__offsetof(SystemState, csselr_el1) == SS_CSSELR_EL1, "");
static_assert(__offsetof(SystemState, esr_el1) == SS_ESR_EL1, "");
static_assert(__offsetof(SystemState, far_el1) == SS_FAR_EL1, "");
static_assert(__offsetof(SystemState, mair_el1) == SS_MAIR_EL1, "");
static_assert(__offsetof(SystemState, mdscr_el1) == SS_MDSCR_EL1, "");
static_assert(__offsetof(SystemState, sctlr_el1) == SS_SCTLR_EL1, "");
static_assert(__offsetof(SystemState, sp_el1) == SS_SP_EL1, "");
static_assert(__offsetof(SystemState, tpidr_el1) == SS_TPIDR_EL1, "");
static_assert(__offsetof(SystemState, tcr_el1) == SS_TCR_EL1, "");
static_assert(__offsetof(SystemState, ttbr0_el1) == SS_TTBR0_EL1, "");
static_assert(__offsetof(SystemState, ttbr1_el1) == SS_TTBR1_EL1, "");
static_assert(__offsetof(SystemState, vbar_el1) == SS_VBAR_EL1, "");
static_assert(__offsetof(SystemState, elr_el2) == SS_ELR_EL2, "");
static_assert(__offsetof(SystemState, spsr_el2) == SS_SPSR_EL2, "");
static_assert(__offsetof(SystemState, vmpidr_el2) == SS_VMPIDR_EL2, "");

static_assert(__offsetof(El2State, esr_el2) == ES_ESR_EL2, "");

static_assert(__offsetof(El2State, host_state.r) == HS_R19, "");
static_assert(__offsetof(El2State, host_state.system_state) == HS_SYSTEM_STATE, "");

static_assert(__offsetof(El2State, guest_state.r) == GS_R0, "");
static_assert(__offsetof(El2State, guest_state.system_state) == GS_SYSTEM_STATE, "");

__BEGIN_CDECLS

extern zx_status_t arm64_el2_on(zx_paddr_t stack_top);
extern zx_status_t arm64_el2_off();
extern zx_status_t arm64_el2_resume(zx_paddr_t el2_state);

__END_CDECLS

#endif // ASSEMBLY
