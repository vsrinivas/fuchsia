// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vmexit_priv.h"

#include <bits.h>
#include <trace.h>

#include <arch/arm64/el2_state.h>
#include <hypervisor/guest_physical_address_space.h>
#include <hypervisor/trap_map.h>
#include <vm/fault.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>

#define LOCAL_TRACE 0

#define SET_SYSREG(sysreg) \
({ \
    guest_state->system_state.sysreg = *si.reg; \
    LTRACEF("guest " #sysreg ": %#lx\n", guest_state->system_state.sysreg); \
    next_pc(guest_state); \
    ZX_OK; \
})

ExceptionSyndrome::ExceptionSyndrome(uint32_t esr) {
    ec = static_cast<ExceptionClass>(BITS_SHIFT(esr, 31, 26));
    iss = BITS(esr, 24, 0);
}

SystemInstruction::SystemInstruction(uint32_t iss, GuestState* guest_state) {
    sysreg = static_cast<SystemRegister>(BITS(iss, 21, 10) >> 6 | BITS_SHIFT(iss, 4, 1));
    reg = &guest_state->x[BITS_SHIFT(iss, 9, 5)];
    read = BIT(iss, 0);
}

static void next_pc(GuestState* guest_state) {
    guest_state->system_state.elr_el2 += 4;
}

static zx_status_t handle_system_instruction(uint32_t iss, fbl::atomic<uint64_t>* hcr,
                                             GuestState* guest_state) {
    SystemInstruction si(iss, guest_state);

    switch (si.sysreg) {
    case SystemRegister::MAIR_EL1:
        return SET_SYSREG(mair_el1);
    case SystemRegister::SCTLR_EL1: {
        if (si.read)
            return ZX_ERR_NOT_SUPPORTED;

        // From ARM DDI 0487B.b, Section D10.2.89: If the value of HCR_EL2.{DC,
        // TGE} is not {0, 0} then in Non-secure state the PE behaves as if the
        // value of the SCTLR_EL1.M field is 0 for all purposes other than
        // returning the value of a direct read of the field.
        //
        // We do not set HCR_EL2.TGE, so we only need to modify HCR_EL2.DC.
        //
        // TODO(abdulla): Investigate clean of cache and invalidation of TLB.
        uint32_t sctlr_el1 = *si.reg & UINT32_MAX;
        if ((guest_state->system_state.sctlr_el1 ^ sctlr_el1) & SCTLR_ELX_M) {
            if (sctlr_el1 & SCTLR_ELX_M) {
                hcr->fetch_and(~HCR_EL2_DC);
            } else {
                hcr->fetch_or(HCR_EL2_DC);
            }
        }
        guest_state->system_state.sctlr_el1 = sctlr_el1;

        LTRACEF("guest sctlr_el1: %#x\n", sctlr_el1);
        LTRACEF("guest hcr_el2: %#lx\n", hcr->load());
        next_pc(guest_state);
        return ZX_OK;
    }
    case SystemRegister::TCR_EL1:
        return SET_SYSREG(tcr_el1);
    case SystemRegister::TTBR0_EL1:
        return SET_SYSREG(ttbr0_el1);
    case SystemRegister::TTBR1_EL1:
        return SET_SYSREG(ttbr1_el1);
    }

    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t handle_page_fault(zx_vaddr_t guest_paddr, GuestPhysicalAddressSpace* gpas) {
    uint pf_flags = VMM_PF_FLAG_HW_FAULT | VMM_PF_FLAG_WRITE | VMM_PF_FLAG_INSTRUCTION;
    return vmm_guest_page_fault_handler(guest_paddr, pf_flags, gpas->aspace());
}

static zx_status_t handle_instruction_abort(GuestState* guest_state,
                                            GuestPhysicalAddressSpace* gpas) {
    return handle_page_fault(guest_state->hpfar_el2, gpas);
}

static zx_status_t handle_data_abort(GuestState* guest_state, GuestPhysicalAddressSpace* gpas,
                                     TrapMap* traps, zx_port_packet_t* packet) {
    zx_vaddr_t guest_paddr = guest_state->hpfar_el2;
    Trap* trap;
    zx_status_t status = traps->FindTrap(ZX_GUEST_TRAP_BELL, guest_paddr, &trap);
    switch (status) {
    case ZX_ERR_NOT_FOUND:
        return handle_page_fault(guest_paddr, gpas);
    case ZX_OK:
        break;
    default:
        return status;
    }
    next_pc(guest_state);

    switch (trap->kind()) {
    case ZX_GUEST_TRAP_BELL:
        *packet = {};
        packet->key = trap->key();
        packet->type = ZX_PKT_TYPE_GUEST_BELL;
        packet->guest_bell.addr = guest_paddr;
        if (trap->HasPort())
            return trap->Queue(*packet, nullptr);
        // If there was no port for the range, then return to user-space.
        break;
    case ZX_GUEST_TRAP_MEM:
        *packet = {};
        packet->key = trap->key();
        packet->type = ZX_PKT_TYPE_GUEST_MEM;
        packet->guest_mem.addr = guest_paddr;
        // TODO(abdulla): Fetch instruction, or consider an alternative.
        break;
    default:
        return ZX_ERR_BAD_STATE;
    }

    return ZX_ERR_NEXT;
}

zx_status_t vmexit_handler(fbl::atomic<uint64_t>* hcr, GuestState* guest_state,
                           GuestPhysicalAddressSpace* gpas, TrapMap* traps,
                           zx_port_packet_t* packet) {
    LTRACEF("guest esr_el1: %#x\n", guest_state->system_state.esr_el1);
    LTRACEF("guest esr_el2: %#x\n", guest_state->esr_el2);
    LTRACEF("guest elr_el2: %#lx\n", guest_state->system_state.elr_el2);
    LTRACEF("guest spsr_el2: %#x\n", guest_state->system_state.spsr_el2);

    ExceptionSyndrome syndrome(guest_state->esr_el2);
    switch (syndrome.ec) {
    case ExceptionClass::SMC_INSTRUCTION:
        LTRACEF("handling smc instruction, iss %#x func %#lx\n", syndrome.iss, guest_state->x[0]);
        return ZX_ERR_NOT_SUPPORTED;
    case ExceptionClass::SYSTEM_INSTRUCTION:
        LTRACEF("handling system instruction\n");
        return handle_system_instruction(syndrome.iss, hcr, guest_state);
    case ExceptionClass::INSTRUCTION_ABORT:
        LTRACEF("handling instruction abort at %#lx\n", guest_state->hpfar_el2);
        return handle_instruction_abort(guest_state, gpas);
    case ExceptionClass::DATA_ABORT:
        LTRACEF("handling data abort at %#lx\n", guest_state->hpfar_el2);
        return handle_data_abort(guest_state, gpas, traps, packet);
    default:
        LTRACEF("unhandled exception syndrome, ec %#x iss %#x\n",
            static_cast<uint32_t>(syndrome.ec), syndrome.iss);
        return ZX_ERR_NOT_SUPPORTED;
    }
}
