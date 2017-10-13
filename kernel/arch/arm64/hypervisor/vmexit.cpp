// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vmexit_priv.h"

#include <arch/arm64/el2_state.h>
#include <hypervisor/trap_map.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>

static const uint32_t kEsrEcShift = 26;
static const uint32_t kEsrIssMask = 0x01ffffff;

ExceptionSyndrome::ExceptionSyndrome(uint32_t esr) {
    ec = static_cast<ExceptionClass>(esr >> kEsrEcShift);
    iss = esr & kEsrIssMask;
}

static void next_pc(GuestState* state) {
    state->system_state.elr_el2 += 4;
}

static zx_status_t handle_instruction_abort(GuestState* guest_state) {
    dprintf(SPEW, "Instruction abort at %#lx\n", guest_state->hpfar_el2);
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t handle_data_abort(GuestState* guest_state, GuestPhysicalAddressSpace* gpas,
                                     TrapMap* traps, zx_port_packet_t* packet) {
    zx_vaddr_t guest_paddr = guest_state->hpfar_el2;
    Trap* trap;
    zx_status_t status = traps->FindTrap(ZX_GUEST_TRAP_BELL, guest_paddr, &trap);
    if (status != ZX_OK)
        return status;
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
    ExceptionSyndrome syndrome(guest_state->esr_el2);
    switch (syndrome.ec) {
    case ExceptionClass::INSTRUCTION_ABORT:
        return handle_instruction_abort(guest_state);
    case ExceptionClass::DATA_ABORT:
        return handle_data_abort(guest_state, gpas, traps, packet);
    default:
        dprintf(SPEW, "Unhandled exception syndrome, ec %#x iss %#x\n",
            static_cast<uint32_t>(syndrome.ec), syndrome.iss);
        return ZX_ERR_NOT_SUPPORTED;
    }
}
