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

ExceptionSyndrome::ExceptionSyndrome(uint32_t esr) {
    ec = static_cast<ExceptionClass>(BITS_SHIFT(esr, 31, 26));
    iss = BITS(esr, 24, 0);
}

DataAbort::DataAbort(uint32_t iss) {
    valid = BIT_SHIFT(iss, 24);
    write = BIT(iss, 6);
}

static void next_pc(GuestState* state) {
    state->system_state.elr_el2 += 4;
}

static zx_status_t handle_page_fault(zx_vaddr_t guest_paddr, GuestPhysicalAddressSpace* gpas,
                                     uint pf_flags) {
    pf_flags |= VMM_PF_FLAG_HW_FAULT;
    return vmm_guest_page_fault_handler(guest_paddr, pf_flags, gpas->aspace());
}

static zx_status_t handle_instruction_abort(uint32_t iss, GuestState* guest_state,
                                            GuestPhysicalAddressSpace* gpas) {
    return handle_page_fault(guest_state->hpfar_el2, gpas, VMM_PF_FLAG_INSTRUCTION);
}

static zx_status_t handle_data_abort(uint32_t iss, GuestState* guest_state,
                                     GuestPhysicalAddressSpace* gpas, TrapMap* traps,
                                     zx_port_packet_t* packet) {
    zx_vaddr_t guest_paddr = guest_state->hpfar_el2;
    Trap* trap;
    zx_status_t status = traps->FindTrap(ZX_GUEST_TRAP_BELL, guest_paddr, &trap);
    switch (status) {
    case ZX_ERR_NOT_FOUND: {
        DataAbort data_abort(iss);
        if (data_abort.valid)
            return ZX_ERR_NOT_SUPPORTED;
        return handle_page_fault(guest_paddr, gpas, data_abort.write ? VMM_PF_FLAG_WRITE : 0);
    }
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

zx_status_t vmexit_handler(GuestState* guest_state, GuestPhysicalAddressSpace* gpas, TrapMap* traps,
                           zx_port_packet_t* packet) {
    LTRACEF("guest esr_el1: %#x\n", guest_state->system_state.esr_el1);
    LTRACEF("guest esr_el2: %#x\n", guest_state->esr_el2);
    LTRACEF("guest elr_el2: %#lx\n", guest_state->system_state.elr_el2);
    LTRACEF("guest spsr_el2: %#x\n", guest_state->system_state.spsr_el2);

    ExceptionSyndrome syndrome(guest_state->esr_el2);
    switch (syndrome.ec) {
    case ExceptionClass::INSTRUCTION_ABORT:
        LTRACEF("handling instruction abort at %#lx\n", guest_state->hpfar_el2);
        return handle_instruction_abort(syndrome.iss, guest_state, gpas);
    case ExceptionClass::DATA_ABORT:
        LTRACEF("handling data abort at %#lx\n", guest_state->hpfar_el2);
        return handle_data_abort(syndrome.iss, guest_state, gpas, traps, packet);
    default:
        LTRACEF("unhandled exception syndrome, ec %#x iss %#x\n",
            static_cast<uint32_t>(syndrome.ec), syndrome.iss);
        return ZX_ERR_NOT_SUPPORTED;
    }
}
