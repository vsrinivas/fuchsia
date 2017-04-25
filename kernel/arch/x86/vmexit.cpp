// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <hypervisor/guest_physical_address_space.h>

#if WITH_LIB_MAGENTA
#include <magenta/fifo_dispatcher.h>
#endif // WITH_LIB_MAGENTA

#include "hypervisor_priv.h"
#include "vmexit_priv.h"

static const uint16_t kUartReceiveIoPort = 0x3f8;
static const uint16_t kUartStatusIoPort = 0x3fd;
static const uint64_t kUartStatusIdle = 1u << 6;
static const uint64_t kIa32ApicBase =
    APIC_PHYS_BASE | IA32_APIC_BASE_BSP | IA32_APIC_BASE_XAPIC_ENABLE;

static void next_rip(const ExitInfo& exit_info) {
    vmcs_write(VmcsFieldXX::GUEST_RIP, exit_info.guest_rip + exit_info.instruction_length);
}

static status_t handle_cpuid(const ExitInfo& exit_info, GuestState* guest_state) {
    const uint64_t leaf = guest_state->rax;
    const uint64_t subleaf = guest_state->rcx;

    switch (leaf) {
    case X86_CPUID_BASE:
    case X86_CPUID_EXT_BASE:
        next_rip(exit_info);
        cpuid((uint32_t)guest_state->rax,
              (uint32_t*)&guest_state->rax, (uint32_t*)&guest_state->rbx,
              (uint32_t*)&guest_state->rcx, (uint32_t*)&guest_state->rdx);
        return NO_ERROR;
    case X86_CPUID_BASE + 1 ... MAX_SUPPORTED_CPUID:
    case X86_CPUID_EXT_BASE + 1 ... MAX_SUPPORTED_CPUID_EXT:
        next_rip(exit_info);
        cpuid_c((uint32_t)guest_state->rax, (uint32_t)guest_state->rcx,
                (uint32_t*)&guest_state->rax, (uint32_t*)&guest_state->rbx,
                (uint32_t*)&guest_state->rcx, (uint32_t*)&guest_state->rdx);
        if (leaf == X86_CPUID_MODEL_FEATURES) {
            // Enable the hypervisor bit.
            guest_state->rcx |= 1u << X86_FEATURE_HYPERVISOR.bit;
            // Disable the x2APIC bit.
            guest_state->rcx &= ~(1u << X86_FEATURE_X2APIC.bit);
        }
        if (leaf == X86_CPUID_XSAVE && subleaf == 1) {
            // Disable the XSAVES bit.
            guest_state->rax &= ~(1u << 3);
        }
        return NO_ERROR;
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static status_t handle_rdmsr(const ExitInfo& exit_info, GuestState* guest_state) {
    switch (guest_state->rcx) {
    case X86_MSR_IA32_APIC_BASE:
        next_rip(exit_info);
        guest_state->rax = kIa32ApicBase;
        guest_state->rdx = 0;
        return NO_ERROR;
    // From Volume 3, Section 28.2.6.2: The MTRRs have no effect on the memory
    // type used for an access to a guest-physical address.
    case X86_MSR_IA32_MTRRCAP:
    case X86_MSR_IA32_MTRR_DEF_TYPE:
    case X86_MSR_IA32_MTRR_FIX64K_00000:
    case X86_MSR_IA32_MTRR_FIX16K_80000 ... X86_MSR_IA32_MTRR_FIX16K_A0000:
    case X86_MSR_IA32_MTRR_FIX4K_C0000 ... X86_MSR_IA32_MTRR_FIX4K_F8000:
    case X86_MSR_IA32_MTRR_PHYSBASE0 ... X86_MSR_IA32_MTRR_PHYSMASK9:
        next_rip(exit_info);
        guest_state->rax = 0;
        guest_state->rdx = 0;
        return NO_ERROR;
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static status_t handle_wrmsr(const ExitInfo& exit_info, GuestState* guest_state) {
    switch (guest_state->rcx) {
    case X86_MSR_IA32_APIC_BASE:
        if (guest_state->rax != kIa32ApicBase || guest_state->rdx != 0)
            return ERR_INVALID_ARGS;
        next_rip(exit_info);
        return NO_ERROR;
    // See note in handle_rdmsr.
    case X86_MSR_IA32_MTRRCAP:
    case X86_MSR_IA32_MTRR_DEF_TYPE:
    case X86_MSR_IA32_MTRR_FIX64K_00000:
    case X86_MSR_IA32_MTRR_FIX16K_80000 ... X86_MSR_IA32_MTRR_FIX16K_A0000:
    case X86_MSR_IA32_MTRR_FIX4K_C0000 ... X86_MSR_IA32_MTRR_FIX4K_F8000:
    case X86_MSR_IA32_MTRR_PHYSBASE0 ... X86_MSR_IA32_MTRR_PHYSMASK9:
        next_rip(exit_info);
        return NO_ERROR;
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static status_t handle_io(const ExitInfo& exit_info, GuestState* guest_state,
                          FifoDispatcher* serial_fifo) {
    next_rip(exit_info);
#if WITH_LIB_MAGENTA
    IoInfo io_info(exit_info.exit_qualification);
    if (io_info.input) {
        if (!io_info.string && !io_info.repeat && io_info.port == kUartStatusIoPort)
            guest_state->rax = kUartStatusIdle;
        return NO_ERROR;
    }
    if (io_info.string || io_info.repeat || io_info.port != kUartReceiveIoPort)
        return NO_ERROR;
    uint8_t* data = reinterpret_cast<uint8_t*>(&guest_state->rax);
    uint32_t actual;
    return serial_fifo->Write(data, io_info.bytes, &actual);
#else // WITH_LIB_MAGENTA
    return NO_ERROR;
#endif // WITH_LIB_MAGENTA
}

static status_t handle_xsetbv(const ExitInfo& exit_info, GuestState* guest_state) {
    uint64_t guest_cr4 = vmcs_read(VmcsFieldXX::GUEST_CR4);
    if (!(guest_cr4 & X86_CR4_OSXSAVE))
        return ERR_INVALID_ARGS;

    // We only support XCR0.
    if (guest_state->rcx != 0)
        return ERR_INVALID_ARGS;

    cpuid_leaf leaf;
    if (!x86_get_cpuid_subleaf(X86_CPUID_XSAVE, 0, &leaf))
        return ERR_INTERNAL;

    // Check that XCR0 is valid.
    uint64_t xcr0_bitmap = ((uint64_t)leaf.d << 32) | leaf.a;
    uint64_t xcr0 = (guest_state->rdx << 32) | (guest_state->rax & 0xffffffff);
    if (~xcr0_bitmap & xcr0 ||
        // x87 state must be enabled.
        (xcr0 & X86_XSAVE_STATE_X87) != X86_XSAVE_STATE_X87 ||
        // If AVX state is enabled, SSE state must be enabled.
        (xcr0 & (X86_XSAVE_STATE_AVX | X86_XSAVE_STATE_SSE)) == X86_XSAVE_STATE_AVX)
        return ERR_INVALID_ARGS;

    guest_state->xcr0 = xcr0;
    next_rip(exit_info);
    return NO_ERROR;
}

status_t vmexit_handler(const VmxState& vmx_state, GuestState* guest_state,
                        GuestPhysicalAddressSpace* gpas, FifoDispatcher* serial_fifo) {
    ExitInfo exit_info;

    switch (exit_info.exit_reason) {
    case ExitReason::EXTERNAL_INTERRUPT:
        dprintf(SPEW, "handling external interrupt\n\n");
        DEBUG_ASSERT(arch_ints_disabled());
        arch_enable_ints();
        arch_disable_ints();
        return NO_ERROR;
    case ExitReason::CPUID:
        dprintf(SPEW, "handling CPUID instruction\n\n");
        return handle_cpuid(exit_info, guest_state);
    case ExitReason::IO_INSTRUCTION:
        return handle_io(exit_info, guest_state, serial_fifo);
    case ExitReason::RDMSR:
        dprintf(SPEW, "handling RDMSR instruction\n\n");
        return handle_rdmsr(exit_info, guest_state);
    case ExitReason::WRMSR:
        dprintf(SPEW, "handling WRMSR instruction\n\n");
        return handle_wrmsr(exit_info, guest_state);
    case ExitReason::ENTRY_FAILURE_GUEST_STATE:
    case ExitReason::ENTRY_FAILURE_MSR_LOADING:
        dprintf(SPEW, "handling VM entry failure\n\n");
        return ERR_BAD_STATE;
    case ExitReason::XSETBV:
        dprintf(SPEW, "handling XSETBV instruction\n\n");
        return handle_xsetbv(exit_info, guest_state);
    default:
        dprintf(SPEW, "unhandled VM exit %u\n\n", static_cast<uint32_t>(exit_info.exit_reason));
        return ERR_NOT_SUPPORTED;
    }
}
