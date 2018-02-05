// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vmexit_priv.h"

#include <bits.h>
#include <trace.h>
#include <platform.h>

#include <arch/arm64/el2_state.h>
#include <arch/hypervisor.h>
#include <dev/psci.h>
#include <dev/timer/arm_generic.h>
#include <hypervisor/guest_physical_address_space.h>
#include <hypervisor/trap_map.h>
#include <vm/fault.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>

#define LOCAL_TRACE 0

#define SET_SYSREG(sysreg)                                                      \
    ({                                                                          \
        guest_state->system_state.sysreg = reg;                                 \
        LTRACEF("guest " #sysreg ": %#lx\n", guest_state->system_state.sysreg); \
        next_pc(guest_state);                                                   \
        ZX_OK;                                                                  \
    })

static constexpr uint16_t kSmcPsci = 0;
static constexpr uint32_t kTimerVector = 27;

static_assert(kTimerVector < kNumInterrupts, "Timer vector is out of range");

enum TimerControl : uint64_t {
    ENABLE = 1u << 0,
    IMASK = 1u << 1,
};

ExceptionSyndrome::ExceptionSyndrome(uint32_t esr) {
    ec = static_cast<ExceptionClass>(BITS_SHIFT(esr, 31, 26));
    iss = BITS(esr, 24, 0);
}

WaitInstruction::WaitInstruction(uint32_t iss) {
    is_wfe = BIT(iss, 0);
}

SmcInstruction::SmcInstruction(uint32_t iss) {
    imm = static_cast<uint16_t>(BITS(iss, 15, 0));
}

SystemInstruction::SystemInstruction(uint32_t iss) {
    sysreg = static_cast<SystemRegister>(BITS(iss, 21, 10) >> 6 | BITS_SHIFT(iss, 4, 1));
    xt = static_cast<uint8_t>(BITS_SHIFT(iss, 9, 5));
    read = BIT(iss, 0);
}

DataAbort::DataAbort(uint32_t iss) {
    valid = BIT_SHIFT(iss, 24);
    access_size = static_cast<uint8_t>(1u << BITS_SHIFT(iss, 23, 22));
    sign_extend = BIT(iss, 21);
    xt = static_cast<uint8_t>(BITS_SHIFT(iss, 20, 16));
    read = !BIT(iss, 6);
}

static void next_pc(GuestState* guest_state) {
    guest_state->system_state.elr_el2 += 4;
}

static void deadline_callback(timer_t* timer, zx_time_t now, void* arg) {
    auto gich_state = static_cast<GichState*>(arg);
    __UNUSED zx_status_t status = gich_state->interrupt_tracker.Interrupt(kTimerVector, nullptr);
    DEBUG_ASSERT(status == ZX_OK);
}

static zx_status_t handle_wfi_wfe_instruction(uint32_t iss, GuestState* guest_state,
                                              GichState* gich_state) {
    next_pc(guest_state);
    const WaitInstruction wi(iss);
    if (wi.is_wfe) {
        thread_reschedule();
        return ZX_OK;
    }

    bool pending = gich_state->active_interrupts.GetOne(kTimerVector);
    bool enabled = guest_state->cntv_ctl_el0 & TimerControl::ENABLE;
    bool masked = guest_state->cntv_ctl_el0 & TimerControl::IMASK;
    if (pending || !enabled || masked) {
        thread_yield();
        return ZX_OK;
    }

    timer_cancel(&gich_state->timer);
    uint64_t cntpct_deadline = guest_state->cntv_cval_el0;
    zx_time_t deadline = cntpct_to_zx_time(cntpct_deadline);
    if (deadline <= current_time()) {
        return gich_state->interrupt_tracker.Track(kTimerVector);
    }

    timer_set_oneshot(&gich_state->timer, deadline, deadline_callback, gich_state);
    return gich_state->interrupt_tracker.Wait(nullptr);
}

static zx_status_t handle_smc_instruction(uint32_t iss, GuestState* guest_state) {
    const SmcInstruction si(iss);
    if (si.imm != kSmcPsci)
        return ZX_ERR_NOT_SUPPORTED;

    next_pc(guest_state);
    switch (guest_state->x[0]) {
    case PSCI64_CPU_ON:
        // Set return value of PSCI call.
        guest_state->x[0] = ZX_ERR_NOT_SUPPORTED;
        return ZX_OK;
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_status_t handle_system_instruction(uint32_t iss, uint64_t* hcr, GuestState* guest_state) {
    const SystemInstruction si(iss);
    const uint64_t reg = guest_state->x[si.xt];

    switch (si.sysreg) {
    case SystemRegister::MAIR_EL1:
        return SET_SYSREG(mair_el1);
    case SystemRegister::SCTLR_EL1: {
        if (si.read) {
            return ZX_ERR_NOT_SUPPORTED;
        }

        // From ARM DDI 0487B.b, Section D10.2.89: If the value of HCR_EL2.{DC,
        // TGE} is not {0, 0} then in Non-secure state the PE behaves as if the
        // value of the SCTLR_EL1.M field is 0 for all purposes other than
        // returning the value of a direct read of the field.
        //
        // Therefore if SCTLR_EL1.M is set to 1, we need to set HCR_EL2.DC to 0.
        uint32_t sctlr_el1 = reg & UINT32_MAX;
        if (sctlr_el1 & SCTLR_ELX_M) {
            *hcr &= ~HCR_EL2_DC;
            // Additionally, if the guest has also set SCTLR_EL1.C to 1, we no
            // longer need to trap writes to virtual memory control registers,
            // so we can set HCR_EL2.TVM to 0 to improve performance.
            if (sctlr_el1 & SCTLR_ELX_C) {
                *hcr &= ~HCR_EL2_TVM;
            }
        }
        guest_state->system_state.sctlr_el1 = sctlr_el1;

        LTRACEF("guest sctlr_el1: %#x\n", sctlr_el1);
        LTRACEF("guest hcr_el2: %#lx\n", *hcr);
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

    dprintf(CRITICAL, "Unhandled system register %#x\n", static_cast<uint16_t>(si.sysreg));
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t handle_page_fault(zx_vaddr_t guest_paddr, GuestPhysicalAddressSpace* gpas) {
    uint pf_flags = VMM_PF_FLAG_HW_FAULT | VMM_PF_FLAG_WRITE | VMM_PF_FLAG_INSTRUCTION;
    return vmm_guest_page_fault_handler(guest_paddr, pf_flags, gpas->aspace());
}

static zx_status_t handle_instruction_abort(GuestState* guest_state,
                                            GuestPhysicalAddressSpace* gpas) {
    zx_status_t status = handle_page_fault(guest_state->hpfar_el2, gpas);
    if (status != ZX_OK) {
        dprintf(CRITICAL, "Unhandled instruction abort %#lx\n",
                guest_state->hpfar_el2);
    }
    return status;
}

static zx_status_t handle_data_abort(uint32_t iss, GuestState* guest_state,
                                     GuestPhysicalAddressSpace* gpas, TrapMap* traps,
                                     zx_port_packet_t* packet) {
    zx_vaddr_t guest_paddr = guest_state->hpfar_el2;
    Trap* trap;
    zx_status_t status = traps->FindTrap(ZX_GUEST_TRAP_BELL, guest_paddr, &trap);
    switch (status) {
    case ZX_ERR_NOT_FOUND:
        status = handle_page_fault(guest_paddr, gpas);
        if (status != ZX_OK) {
            dprintf(CRITICAL, "Unhandled data abort %#lx\n", guest_paddr);
        }
        return status;
    case ZX_OK:
        break;
    default:
        return status;
    }
    next_pc(guest_state);

    // Combine the lower bits of FAR_EL2 with HPFAR_EL2 to get the exact IPA.
    guest_paddr |= guest_state->far_el2 & (PAGE_SIZE - 1);
    LTRACEF("guest far_el2: %#lx\n", guest_state->far_el2);

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
    case ZX_GUEST_TRAP_MEM: {
        *packet = {};
        packet->key = trap->key();
        packet->type = ZX_PKT_TYPE_GUEST_MEM;
        packet->guest_mem.addr = guest_paddr;
        const DataAbort data_abort(iss);
        if (!data_abort.valid)
            return ZX_ERR_IO_DATA_INTEGRITY;
        packet->guest_mem.access_size = data_abort.access_size;
        packet->guest_mem.sign_extend = data_abort.sign_extend;
        packet->guest_mem.xt = data_abort.xt;
        packet->guest_mem.read = data_abort.read;
        if (!data_abort.read)
            packet->guest_mem.data = guest_state->x[data_abort.xt];
        break;
    }
    default:
        return ZX_ERR_BAD_STATE;
    }

    return ZX_ERR_NEXT;
}

zx_status_t vmexit_handler(uint64_t* hcr, GuestState* guest_state, GichState* gich_state,
                           GuestPhysicalAddressSpace* gpas, TrapMap* traps,
                           zx_port_packet_t* packet) {
    LTRACEF("guest esr_el1: %#x\n", guest_state->system_state.esr_el1);
    LTRACEF("guest esr_el2: %#x\n", guest_state->esr_el2);
    LTRACEF("guest elr_el2: %#lx\n", guest_state->system_state.elr_el2);
    LTRACEF("guest spsr_el2: %#x\n", guest_state->system_state.spsr_el2);

    ExceptionSyndrome syndrome(guest_state->esr_el2);
    switch (syndrome.ec) {
    case ExceptionClass::WFI_WFE_INSTRUCTION:
        LTRACEF("handling wfi/wfe instruction, iss %#x\n", syndrome.iss);
        return handle_wfi_wfe_instruction(syndrome.iss, guest_state, gich_state);
    case ExceptionClass::SMC_INSTRUCTION:
        LTRACEF("handling smc instruction, iss %#x func %#lx\n", syndrome.iss, guest_state->x[0]);
        return handle_smc_instruction(syndrome.iss, guest_state);
    case ExceptionClass::SYSTEM_INSTRUCTION:
        LTRACEF("handling system instruction\n");
        return handle_system_instruction(syndrome.iss, hcr, guest_state);
    case ExceptionClass::INSTRUCTION_ABORT:
        LTRACEF("handling instruction abort at %#lx\n", guest_state->hpfar_el2);
        return handle_instruction_abort(guest_state, gpas);
    case ExceptionClass::DATA_ABORT:
        LTRACEF("handling data abort at %#lx\n", guest_state->hpfar_el2);
        return handle_data_abort(syndrome.iss, guest_state, gpas, traps, packet);
    default:
        LTRACEF("unhandled exception syndrome, ec %#x iss %#x\n",
                static_cast<uint32_t>(syndrome.ec), syndrome.iss);
        return ZX_ERR_NOT_SUPPORTED;
    }
}
