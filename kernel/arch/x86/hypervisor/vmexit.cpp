// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vmexit_priv.h"

#include <bits.h>
#include <inttypes.h>
#include <string.h>
#include <trace.h>

#include <arch/hypervisor.h>
#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <arch/x86/mmu.h>
#include <arch/x86/pvclock.h>
#include <explicit-memory/bytes.h>
#include <fbl/canary.h>
#include <hypervisor/interrupt_tracker.h>
#include <hypervisor/ktrace.h>
#include <kernel/auto_lock.h>
#include <lib/ktrace.h>
#include <platform.h>
#include <platform/pc/timer.h>
#include <vm/fault.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/types.h>

#include "pvclock_priv.h"
#include "vcpu_priv.h"

#define LOCAL_TRACE 0

static const uint64_t kLocalApicPhysBase =
    APIC_PHYS_BASE | IA32_APIC_BASE_XAPIC_ENABLE | IA32_APIC_BASE_X2APIC_ENABLE;

static const uint64_t kX2ApicMsrBase = 0x800;
static const uint64_t kX2ApicMsrMax = 0x83f;

static const uint64_t kMiscEnableFastStrings = 1u << 0;

static const uint32_t kFirstExtendedStateComponent = 2;
static const uint32_t kLastExtendedStateComponent = 9;
// From Volume 1, Section 13.4.
static const uint32_t kXsaveLegacyRegionSize = 512;
static const uint32_t kXsaveHeaderSize = 64;

static const char kHypVendorId[] = "KVMKVMKVM\0\0\0";
static const size_t kHypVendorIdLength = 12;
static_assert(sizeof(kHypVendorId) - 1 == kHypVendorIdLength, "");

extern "C" void x86_call_external_interrupt_handler(uint64_t vector);

ExitInfo::ExitInfo(const AutoVmcs& vmcs) {
    // From Volume 3, Section 26.7.
    uint32_t full_exit_reason = vmcs.Read(VmcsField32::EXIT_REASON);
    entry_failure = BIT(full_exit_reason, 31);
    exit_reason = static_cast<ExitReason>(BITS(full_exit_reason, 15, 0));

    exit_qualification = vmcs.Read(VmcsFieldXX::EXIT_QUALIFICATION);
    exit_instruction_length = vmcs.Read(VmcsField32::EXIT_INSTRUCTION_LENGTH);
    guest_physical_address = vmcs.Read(VmcsField64::GUEST_PHYSICAL_ADDRESS);
    guest_rip = vmcs.Read(VmcsFieldXX::GUEST_RIP);

    if (exit_reason == ExitReason::EXTERNAL_INTERRUPT ||
        exit_reason == ExitReason::IO_INSTRUCTION)
        return;

    LTRACEF("entry failure: %d\n", entry_failure);
    LTRACEF("exit reason: %#x (%s)\n", static_cast<uint32_t>(exit_reason), exit_reason_name(exit_reason));
    LTRACEF("exit qualification: %#lx\n", exit_qualification);
    LTRACEF("exit instruction length: %#x\n", exit_instruction_length);
    LTRACEF("guest activity state: %#x\n", vmcs.Read(VmcsField32::GUEST_ACTIVITY_STATE));
    LTRACEF("guest interruptibility state: %#x\n",
            vmcs.Read(VmcsField32::GUEST_INTERRUPTIBILITY_STATE));
    LTRACEF("guest physical address: %#lx\n", guest_physical_address);
    LTRACEF("guest linear address: %#lx\n", vmcs.Read(VmcsFieldXX::GUEST_LINEAR_ADDRESS));
    LTRACEF("guest rip: %#lx\n", guest_rip);
}

ExitInterruptionInformation::ExitInterruptionInformation(const AutoVmcs& vmcs) {
    uint32_t int_info = vmcs.Read(VmcsField32::EXIT_INTERRUPTION_INFORMATION);
    vector = static_cast<uint8_t>(BITS(int_info, 7, 0));
    interruption_type = static_cast<InterruptionType>(BITS_SHIFT(int_info, 10, 8));
    valid = BIT(int_info, 31);
};

CrAccessInfo::CrAccessInfo(uint64_t qualification) {
    // From Volume 3, Table 27-3.
    cr_number = static_cast<uint8_t>(BITS(qualification, 3, 0));
    access_type = static_cast<CrAccessType>(BITS_SHIFT(qualification, 5, 4));
    reg = static_cast<uint8_t>(BITS_SHIFT(qualification, 11, 8));
}

IoInfo::IoInfo(uint64_t qualification) {
    access_size = static_cast<uint8_t>(BITS(qualification, 2, 0) + 1);
    input = BIT_SHIFT(qualification, 3);
    string = BIT_SHIFT(qualification, 4);
    repeat = BIT_SHIFT(qualification, 5);
    port = static_cast<uint16_t>(BITS_SHIFT(qualification, 31, 16));
}

EptViolationInfo::EptViolationInfo(uint64_t qualification) {
    // From Volume 3C, Table 27-7.
    read = BIT(qualification, 0);
    write = BIT(qualification, 1);
    instruction = BIT(qualification, 2);
}

InterruptCommandRegister::InterruptCommandRegister(uint32_t hi, uint32_t lo) {
    destination = hi;
    destination_mode = static_cast<InterruptDestinationMode>(BIT_SHIFT(lo, 11));
    delivery_mode = static_cast<InterruptDeliveryMode>(BITS_SHIFT(lo, 10, 8));
    destination_shorthand = static_cast<InterruptDestinationShorthand>(BITS_SHIFT(lo, 19, 18));
    vector = static_cast<uint8_t>(BITS(lo, 7, 0));
}

static void next_rip(const ExitInfo& exit_info, AutoVmcs* vmcs) {
    vmcs->Write(VmcsFieldXX::GUEST_RIP, exit_info.guest_rip + exit_info.exit_instruction_length);

    // Clear any flags blocking interrupt injection for a single instruction.
    uint32_t guest_interruptibility = vmcs->Read(VmcsField32::GUEST_INTERRUPTIBILITY_STATE);
    uint32_t new_interruptibility = guest_interruptibility &
                                    ~(kInterruptibilityStiBlocking | kInterruptibilityMovSsBlocking);
    if (new_interruptibility != guest_interruptibility) {
        vmcs->Write(VmcsField32::GUEST_INTERRUPTIBILITY_STATE, new_interruptibility);
    }
}

static zx_status_t handle_external_interrupt(AutoVmcs* vmcs, LocalApicState* local_apic_state) {
    ExitInterruptionInformation int_info(*vmcs);
    DEBUG_ASSERT(int_info.valid);
    DEBUG_ASSERT(int_info.interruption_type == InterruptionType::EXTERNAL_INTERRUPT);
    x86_call_external_interrupt_handler(int_info.vector);
    vmcs->Invalidate();

    // If we are receiving an external interrupt because the thread is being
    // killed, we should exit with an error.
    return get_current_thread()->signals & THREAD_SIGNAL_KILL ? ZX_ERR_CANCELED : ZX_OK;
}

static zx_status_t handle_interrupt_window(AutoVmcs* vmcs, LocalApicState* local_apic_state) {
    vmcs->InterruptWindowExiting(false);
    return ZX_OK;
}

// From Volume 2, Section 3.2, Table 3-8  "Processor Extended State Enumeration
// Main Leaf (EAX = 0DH, ECX = 0)".
//
// Bits 31-00: Maximum size (bytes, from the beginning of the XSAVE/XRSTOR save
// area) required by enabled features in XCR0. May be different than ECX if some
// features at the end of the XSAVE save area are not enabled.
static zx_status_t compute_xsave_size(uint64_t guest_xcr0, uint32_t* xsave_size) {
    *xsave_size = kXsaveLegacyRegionSize + kXsaveHeaderSize;
    for (uint32_t i = kFirstExtendedStateComponent; i <= kLastExtendedStateComponent; ++i) {
        cpuid_leaf leaf;
        if (!(guest_xcr0 & (1 << i)))
            continue;
        if (!x86_get_cpuid_subleaf(X86_CPUID_XSAVE, i, &leaf))
            return ZX_ERR_INTERNAL;
        if (leaf.a == 0 && leaf.b == 0 && leaf.c == 0 && leaf.d == 0)
            continue;
        const uint32_t component_offset = leaf.b;
        const uint32_t component_size = leaf.a;
        *xsave_size = component_offset + component_size;
    }
    return ZX_OK;
}

static zx_status_t handle_cpuid(const ExitInfo& exit_info, AutoVmcs* vmcs,
                                GuestState* guest_state) {
    const uint32_t leaf = static_cast<uint32_t>(guest_state->rax);
    const uint32_t subleaf = static_cast<uint32_t>(guest_state->rcx);

    next_rip(exit_info, vmcs);
    switch (leaf) {
    case X86_CPUID_BASE:
    case X86_CPUID_EXT_BASE:
        cpuid(leaf,
              reinterpret_cast<uint32_t*>(&guest_state->rax),
              reinterpret_cast<uint32_t*>(&guest_state->rbx),
              reinterpret_cast<uint32_t*>(&guest_state->rcx),
              reinterpret_cast<uint32_t*>(&guest_state->rdx));
        return ZX_OK;
    case X86_CPUID_BASE + 1 ... MAX_SUPPORTED_CPUID:
    case X86_CPUID_EXT_BASE + 1 ... MAX_SUPPORTED_CPUID_EXT:
        cpuid_c(leaf, subleaf,
                reinterpret_cast<uint32_t*>(&guest_state->rax),
                reinterpret_cast<uint32_t*>(&guest_state->rbx),
                reinterpret_cast<uint32_t*>(&guest_state->rcx),
                reinterpret_cast<uint32_t*>(&guest_state->rdx));
        switch (leaf) {
        case X86_CPUID_MODEL_FEATURES:
            // Enable the hypervisor bit.
            guest_state->rcx |= 1u << X86_FEATURE_HYPERVISOR.bit;
            // Enable the x2APIC bit.
            guest_state->rcx |= 1u << X86_FEATURE_X2APIC.bit;
            // Disable the VMX bit.
            guest_state->rcx &= ~(1u << X86_FEATURE_VMX.bit);
            // Disable the PDCM bit.
            guest_state->rcx &= ~(1u << X86_FEATURE_PDCM.bit);
            // Disable MONITOR/MWAIT.
            guest_state->rcx &= ~(1u << X86_FEATURE_MON.bit);
            // Disable the SEP (SYSENTER support).
            guest_state->rdx &= ~(1u << X86_FEATURE_SEP.bit);
            // Disable the Thermal Monitor bit.
            guest_state->rdx &= ~(1u << X86_FEATURE_TM.bit);
            break;
        case X86_CPUID_XSAVE:
            if (subleaf == 0) {
                uint32_t xsave_size = 0;
                zx_status_t status = compute_xsave_size(guest_state->xcr0, &xsave_size);
                if (status != ZX_OK)
                    return status;
                guest_state->rbx = xsave_size;
            } else if (subleaf == 1) {
                guest_state->rax &= ~(1u << 3);
            }
            break;
        case X86_CPUID_THERMAL_AND_POWER:
            // Disable the performance energy bias bit.
            guest_state->rcx &= ~(1u << X86_FEATURE_PERF_BIAS.bit);
            // Disable the hardware coordination feedback bit.
            guest_state->rcx &= ~(1u << X86_FEATURE_HW_FEEDBACK.bit);
            // Disable HWP MSRs.
            guest_state->rax &= ~(
                1u << X86_FEATURE_HWP.bit |
                1u << X86_FEATURE_HWP_NOT.bit |
                1u << X86_FEATURE_HWP_ACT.bit |
                1u << X86_FEATURE_HWP_PREF.bit);
            break;
        case X86_CPUID_PERFORMANCE_MONITORING: {
            // Disable all performance monitoring.
            // 31-07 = Reserved 0, 06-00 = 1 if event is not available.
            const uint32_t performance_monitoring_no_events = 0b1111111;
            guest_state->rax = 0;
            guest_state->rbx = performance_monitoring_no_events;
            guest_state->rcx = 0;
            guest_state->rdx = 0;
            break;
        }
        case X86_CPUID_MON:
            // MONITOR/MWAIT are not implemented.
            guest_state->rax = 0;
            guest_state->rbx = 0;
            guest_state->rcx = 0;
            guest_state->rdx = 0;
            break;
        case X86_CPUID_EXTENDED_FEATURE_FLAGS:
            // It's possible when running under KVM in nVMX mode, that host
            // CPUID indicates that invpcid is supported but VMX doesn't allow
            // to enable INVPCID bit in secondary processor based controls.
            // Therefore explicitly clear INVPCID bit in CPUID if the VMX flag
            // wasn't set.
            if ((vmcs->Read(VmcsField32::PROCBASED_CTLS2) & kProcbasedCtls2Invpcid) == 0)
                guest_state->rbx &= ~(1u << X86_FEATURE_INVPCID.bit);
            // Disable the Processor Trace bit.
            guest_state->rbx &= ~(1u << X86_FEATURE_PT.bit);
            break;
        }
        return ZX_OK;
    case X86_CPUID_HYP_VENDOR: {
        // This leaf is commonly used to identify a hypervisor via ebx:ecx:edx.
        static const uint32_t* regs = reinterpret_cast<const uint32_t*>(kHypVendorId);
        // Since Zircon hypervisor disguises itself as KVM, it needs to return
        // in EAX max CPUID function supported by hypervisor. Zero in EAX
        // should be interpreted as 0x40000001. Details are available in the
        // Linux kernel documentation (Documentation/virtual/kvm/cpuid.txt).
        guest_state->rax = X86_CPUID_KVM_FEATURES;
        guest_state->rbx = regs[0];
        guest_state->rcx = regs[1];
        guest_state->rdx = regs[2];
        return ZX_OK;
    }
    case X86_CPUID_KVM_FEATURES:
        // We support KVM clock.
        guest_state->rax = kKvmFeatureClockSourceOld | kKvmFeatureClockSource;
        guest_state->rbx = 0;
        guest_state->rcx = 0;
        guest_state->rdx = 0;
        return ZX_OK;
    // From Volume 2A, CPUID instruction reference. If the EAX value is outside
    // the range recognized by CPUID then the information for the highest
    // supported base information leaf is returned. Any value in ECX is
    // honored.
    default:
        cpuid_c(MAX_SUPPORTED_CPUID, subleaf,
                reinterpret_cast<uint32_t*>(&guest_state->rax),
                reinterpret_cast<uint32_t*>(&guest_state->rbx),
                reinterpret_cast<uint32_t*>(&guest_state->rcx),
                reinterpret_cast<uint32_t*>(&guest_state->rdx));
        return ZX_OK;
    }
}

static zx_status_t handle_hlt(const ExitInfo& exit_info, AutoVmcs* vmcs,
                              LocalApicState* local_apic_state) {
    next_rip(exit_info, vmcs);
    return local_apic_state->interrupt_tracker.Wait(vmcs);
}

static zx_status_t handle_cr0_write(AutoVmcs* vmcs, GuestState* guest_state, uint64_t val) {
    // Ensure that CR0.NE is set since it is set in X86_MSR_IA32_VMX_CR0_FIXED1.
    uint64_t cr0 = val | X86_CR0_NE;
    if (cr0_is_invalid(vmcs, cr0)) {
        return ZX_ERR_INVALID_ARGS;
    }
    vmcs->Write(VmcsFieldXX::GUEST_CR0, cr0);
    // From Volume 3, Section 26.3.1.1: If CR0.PG and EFER.LME are set then EFER.LMA and the IA-32e
    // mode guest entry control must also be set.
    uint64_t efer = vmcs->Read(VmcsField64::GUEST_IA32_EFER);
    if (!(efer & X86_EFER_LME && cr0 & X86_CR0_PG)) {
        return ZX_OK;
    }
    vmcs->Write(VmcsField64::GUEST_IA32_EFER, efer | X86_EFER_LMA);
    return vmcs->SetControl(VmcsField32::ENTRY_CTLS,
                            read_msr(X86_MSR_IA32_VMX_TRUE_ENTRY_CTLS),
                            read_msr(X86_MSR_IA32_VMX_ENTRY_CTLS),
                            kEntryCtlsIa32eMode, 0);
}

static zx_status_t register_value(AutoVmcs* vmcs, GuestState* guest_state, uint8_t register_id,
                                  uint64_t* out) {
    switch (register_id) {
    // From Intel Volume 3, Table 27-3.
    case 0:
        *out = guest_state->rax;
        return ZX_OK;
    case 1:
        *out = guest_state->rcx;
        return ZX_OK;
    case 2:
        *out = guest_state->rdx;
        return ZX_OK;
    case 3:
        *out = guest_state->rbx;
        return ZX_OK;
    case 4:
        *out = vmcs->Read(VmcsFieldXX::GUEST_RSP);
        return ZX_OK;
    case 5:
        *out = guest_state->rbp;
        return ZX_OK;
    case 6:
        *out = guest_state->rsi;
        return ZX_OK;
    case 7:
        *out = guest_state->rdi;
        return ZX_OK;
    case 8:
        *out = guest_state->r8;
        return ZX_OK;
    case 9:
        *out = guest_state->r9;
        return ZX_OK;
    case 10:
        *out = guest_state->r10;
        return ZX_OK;
    case 11:
        *out = guest_state->r11;
        return ZX_OK;
    case 12:
        *out = guest_state->r12;
        return ZX_OK;
    case 13:
        *out = guest_state->r13;
        return ZX_OK;
    case 14:
        *out = guest_state->r14;
        return ZX_OK;
    case 15:
        *out = guest_state->r15;
        return ZX_OK;
    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

static zx_status_t handle_control_register_access(const ExitInfo& exit_info, AutoVmcs* vmcs,
                                                  GuestState* guest_state) {
    CrAccessInfo cr_access_info(exit_info.exit_qualification);
    switch (cr_access_info.access_type) {
    case CrAccessType::MOV_TO_CR: {
        // Handle CR0 only.
        if (cr_access_info.cr_number != 0) {
            return ZX_ERR_NOT_SUPPORTED;
        }
        uint64_t val;
        zx_status_t status = register_value(vmcs, guest_state, cr_access_info.reg, &val);
        if (status != ZX_OK) {
            return status;
        }
        status = handle_cr0_write(vmcs, guest_state, val);
        if (status != ZX_OK) {
            return status;
        }
        next_rip(exit_info, vmcs);
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_status_t handle_io_instruction(const ExitInfo& exit_info, AutoVmcs* vmcs,
                                         GuestState* guest_state, hypervisor::TrapMap* traps,
                                         zx_port_packet_t* packet) {
    IoInfo io_info(exit_info.exit_qualification);
    if (io_info.string || io_info.repeat) {
        dprintf(CRITICAL, "Unsupported IO instruction\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    hypervisor::Trap* trap;
    zx_status_t status = traps->FindTrap(ZX_GUEST_TRAP_IO, io_info.port, &trap);
    if (status != ZX_OK) {
        dprintf(CRITICAL, "Unhandled IO port %s %#x\n",
                io_info.input ? "in" : "out", io_info.port);
        return status;
    }
    next_rip(exit_info, vmcs);

    memset(packet, 0, sizeof(*packet));
    packet->key = trap->key();
    packet->type = ZX_PKT_TYPE_GUEST_IO;
    packet->guest_io.port = io_info.port;
    packet->guest_io.access_size = io_info.access_size;
    packet->guest_io.input = io_info.input;
    if (io_info.input) {
        // From Volume 1, Section 3.4.1.1: 32-bit operands generate a 32-bit
        // result, zero-extended to a 64-bit result in the destination general-
        // purpose register.
        if (io_info.access_size == 4)
            guest_state->rax = 0;
    } else {
        memcpy(packet->guest_io.data, &guest_state->rax, io_info.access_size);
        if (trap->HasPort())
            return trap->Queue(*packet, vmcs);
        // If there was no port for the range, then return to user-space.
    }

    return ZX_ERR_NEXT;
}

static zx_status_t handle_apic_rdmsr(const ExitInfo& exit_info, AutoVmcs* vmcs,
                                     GuestState* guest_state, LocalApicState* local_apic_state) {
    switch (static_cast<X2ApicMsr>(guest_state->rcx)) {
    case X2ApicMsr::ID:
        next_rip(exit_info, vmcs);
        guest_state->rax = vmcs->Read(VmcsField16::VPID) - 1;
        return ZX_OK;
    case X2ApicMsr::VERSION: {
        next_rip(exit_info, vmcs);
        // We choose 15H as it causes us to be seen as a modern APIC by Linux,
        // and is the highest non-reserved value. See Volume 3 Section 10.4.8.
        const uint32_t version = 0x15;
        const uint32_t max_lvt_entry = 0x6; // LVT entries minus 1.
        const uint32_t eoi_suppression = 0; // Disable support for EOI-broadcast suppression.
        guest_state->rax = version | (max_lvt_entry << 16) | (eoi_suppression << 24);
        return ZX_OK;
    }
    case X2ApicMsr::SVR:
        // Spurious interrupt vector resets to 0xff. See Volume 3 Section 10.12.5.1.
        next_rip(exit_info, vmcs);
        guest_state->rax = 0xff;
        return ZX_OK;
    case X2ApicMsr::TPR:
    case X2ApicMsr::LDR:
    case X2ApicMsr::ISR_31_0... X2ApicMsr::ISR_255_224:
    case X2ApicMsr::TMR_31_0... X2ApicMsr::TMR_255_224:
    case X2ApicMsr::IRR_31_0... X2ApicMsr::IRR_255_224:
    case X2ApicMsr::ESR:
    case X2ApicMsr::LVT_MONITOR:
        // These registers reset to 0. See Volume 3 Section 10.12.5.1.
        next_rip(exit_info, vmcs);
        guest_state->rax = 0;
        return ZX_OK;
    case X2ApicMsr::LVT_LINT0:
    case X2ApicMsr::LVT_LINT1:
    case X2ApicMsr::LVT_THERMAL_SENSOR:
    case X2ApicMsr::LVT_CMCI:
        // LVT registers reset with the mask bit set. See Volume 3 Section 10.12.5.1.
        next_rip(exit_info, vmcs);
        guest_state->rax = LVT_MASKED;
        return ZX_OK;
    case X2ApicMsr::LVT_TIMER:
        next_rip(exit_info, vmcs);
        guest_state->rax = local_apic_state->lvt_timer;
        return ZX_OK;
    default:
        // Issue a general protection fault for write only and unimplemented
        // registers.
        dprintf(INFO, "Unhandled x2APIC rdmsr %#lx\n", guest_state->rcx);
        return local_apic_state->interrupt_tracker.Interrupt(X86_INT_GP_FAULT, nullptr);
    }
}

static zx_status_t handle_rdmsr(const ExitInfo& exit_info, AutoVmcs* vmcs,
                                GuestState* guest_state, LocalApicState* local_apic_state) {
    switch (guest_state->rcx) {
    case X86_MSR_IA32_APIC_BASE:
        next_rip(exit_info, vmcs);
        guest_state->rax = kLocalApicPhysBase;
        if (vmcs->Read(VmcsField16::VPID) == 1)
            guest_state->rax |= IA32_APIC_BASE_BSP;
        guest_state->rdx = 0;
        return ZX_OK;
    // From Volume 4, Section 2.1, Table 2-2: For now, only enable fast strings.
    case X86_MSR_IA32_MISC_ENABLE:
        next_rip(exit_info, vmcs);
        guest_state->rax = read_msr(X86_MSR_IA32_MISC_ENABLE) & kMiscEnableFastStrings;
        guest_state->rdx = 0;
        return ZX_OK;
    // From Volume 3, Section 28.2.6.2: The MTRRs have no effect on the memory
    // type used for an access to a guest-physical address.
    case X86_MSR_IA32_MTRRCAP:
    case X86_MSR_IA32_MTRR_DEF_TYPE:
    case X86_MSR_IA32_MTRR_FIX64K_00000:
    case X86_MSR_IA32_MTRR_FIX16K_80000... X86_MSR_IA32_MTRR_FIX16K_A0000:
    case X86_MSR_IA32_MTRR_FIX4K_C0000... X86_MSR_IA32_MTRR_FIX4K_F8000:
    case X86_MSR_IA32_MTRR_PHYSBASE0... X86_MSR_IA32_MTRR_PHYSMASK9:
    // From Volume 3, Section 9.11.4: For now, 0.
    case X86_MSR_IA32_PLATFORM_ID:
    // From Volume 3, Section 9.11.7: 0 indicates no microcode update is loaded.
    case X86_MSR_IA32_BIOS_SIGN_ID:
    // From Volume 3, Section 15.3.1: 0 indicates that our machine has no
    // checking capabilities.
    case X86_MSR_IA32_MCG_CAP:
    case X86_MSR_IA32_MCG_STATUS:
    case X86_MSR_IA32_TEMPERATURE_TARGET:
        next_rip(exit_info, vmcs);
        guest_state->rax = 0;
        guest_state->rdx = 0;
        return ZX_OK;
    case kX2ApicMsrBase... kX2ApicMsrMax:
        return handle_apic_rdmsr(exit_info, vmcs, guest_state, local_apic_state);
    default:
        dprintf(INFO, "Unhandled rdmsr %#lx\n", guest_state->rcx);
        return local_apic_state->interrupt_tracker.Interrupt(X86_INT_GP_FAULT, nullptr);
    }
}

zx_time_t lvt_deadline(LocalApicState* local_apic_state) {
    if ((local_apic_state->lvt_timer & LVT_TIMER_MODE_MASK) != LVT_TIMER_MODE_ONESHOT &&
        (local_apic_state->lvt_timer & LVT_TIMER_MODE_MASK) != LVT_TIMER_MODE_PERIODIC) {
        return 0;
    }
    uint32_t shift = BITS_SHIFT(local_apic_state->lvt_divide_config, 1, 0) |
                     (BIT_SHIFT(local_apic_state->lvt_divide_config, 3) << 2);
    uint32_t divisor_shift = (shift + 1) & 7;
    return current_time() + ticks_to_nanos(local_apic_state->lvt_initial_count << divisor_shift);
}

static void update_timer(LocalApicState* local_apic_state, uint64_t deadline);

static void deadline_callback(timer_t* timer, zx_time_t now, void* arg) {
    LocalApicState* local_apic_state = static_cast<LocalApicState*>(arg);
    if (local_apic_state->lvt_timer & LVT_MASKED) {
        return;
    }
    if ((local_apic_state->lvt_timer & LVT_TIMER_MODE_MASK) == LVT_TIMER_MODE_PERIODIC) {
        update_timer(local_apic_state, lvt_deadline(local_apic_state));
    }
    uint8_t vector = local_apic_state->lvt_timer & LVT_TIMER_VECTOR_MASK;
    local_apic_state->interrupt_tracker.Interrupt(vector, nullptr);
}

static void update_timer(LocalApicState* local_apic_state, zx_time_t deadline) {
    timer_cancel(&local_apic_state->timer);
    if (deadline > 0) {
        timer_set_oneshot(&local_apic_state->timer, deadline, deadline_callback, local_apic_state);
    }
}

static uint32_t ipi_target_mask(const InterruptCommandRegister& icr, uint16_t self) {
    switch (icr.destination_shorthand) {
    case InterruptDestinationShorthand::NO_SHORTHAND:
        return 1u << icr.destination;
    case InterruptDestinationShorthand::SELF:
        return 1u << (self - 1);
    case InterruptDestinationShorthand::ALL_INCLUDING_SELF:
        return UINT32_MAX;
    case InterruptDestinationShorthand::ALL_EXCLUDING_SELF:
        return ~(1u << (self - 1));
    }
    return 0;
}

static zx_status_t handle_ipi(const ExitInfo& exit_info, AutoVmcs* vmcs, GuestState* guest_state,
                              zx_port_packet* packet) {
    if (guest_state->rax > UINT32_MAX || guest_state->rdx > UINT32_MAX)
        return ZX_ERR_INVALID_ARGS;
    InterruptCommandRegister icr(static_cast<uint32_t>(guest_state->rdx),
                                 static_cast<uint32_t>(guest_state->rax));
    if (icr.destination_mode == InterruptDestinationMode::LOGICAL) {
        dprintf(CRITICAL, "Logical IPI destination mode is not supported\n");
        return ZX_ERR_NOT_SUPPORTED;
    }
    switch (icr.delivery_mode) {
    case InterruptDeliveryMode::FIXED: {
        uint16_t self = static_cast<uint16_t>(vmcs->Read(VmcsField16::VPID) - 1);
        memset(packet, 0, sizeof(*packet));
        packet->type = ZX_PKT_TYPE_GUEST_VCPU;
        packet->guest_vcpu.type = ZX_PKT_GUEST_VCPU_INTERRUPT;
        packet->guest_vcpu.interrupt.mask = ipi_target_mask(icr, self);
        packet->guest_vcpu.interrupt.vector = icr.vector;
        next_rip(exit_info, vmcs);
        return ZX_ERR_NEXT;
    }
    case InterruptDeliveryMode::INIT:
        // Ignore INIT IPIs, we only need STARTUP to bring up a VCPU.
        next_rip(exit_info, vmcs);
        return ZX_OK;
    case InterruptDeliveryMode::STARTUP:
        memset(packet, 0, sizeof(*packet));
        packet->type = ZX_PKT_TYPE_GUEST_VCPU;
        packet->guest_vcpu.type = ZX_PKT_GUEST_VCPU_STARTUP;
        packet->guest_vcpu.startup.id = icr.destination;
        packet->guest_vcpu.startup.entry = icr.vector << 12;
        next_rip(exit_info, vmcs);
        return ZX_ERR_NEXT;
    default:
        dprintf(CRITICAL, "Unsupported IPI delivery mode %#x\n",
                static_cast<uint8_t>(icr.delivery_mode));
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_status_t handle_apic_wrmsr(const ExitInfo& exit_info, AutoVmcs* vmcs,
                                     GuestState* guest_state, LocalApicState* local_apic_state,
                                     zx_port_packet* packet) {
    switch (static_cast<X2ApicMsr>(guest_state->rcx)) {
    case X2ApicMsr::EOI:
    case X2ApicMsr::ESR:
        if (guest_state->rax != 0) {
            // Non-zero writes to EOI and ESR cause GP fault. See Volume 3 Section 10.12.1.2.
            return local_apic_state->interrupt_tracker.Interrupt(X86_INT_GP_FAULT, nullptr);
        }
    // Fall through.
    case X2ApicMsr::TPR:
    case X2ApicMsr::SVR:
    case X2ApicMsr::LVT_MONITOR:
    case X2ApicMsr::LVT_ERROR:
    case X2ApicMsr::LVT_LINT0:
    case X2ApicMsr::LVT_LINT1:
    case X2ApicMsr::LVT_THERMAL_SENSOR:
    case X2ApicMsr::LVT_CMCI:
        if (guest_state->rdx != 0 || guest_state->rax > UINT32_MAX)
            return ZX_ERR_INVALID_ARGS;
        next_rip(exit_info, vmcs);
        return ZX_OK;
    case X2ApicMsr::LVT_TIMER:
        if (guest_state->rax > UINT32_MAX)
            return ZX_ERR_INVALID_ARGS;
        if ((guest_state->rax & LVT_TIMER_MODE_MASK) == LVT_TIMER_MODE_RESERVED)
            return ZX_ERR_INVALID_ARGS;
        next_rip(exit_info, vmcs);
        local_apic_state->lvt_timer = static_cast<uint32_t>(guest_state->rax);
        update_timer(local_apic_state, lvt_deadline(local_apic_state));
        return ZX_OK;
    case X2ApicMsr::INITIAL_COUNT:
        if (guest_state->rax > UINT32_MAX)
            return ZX_ERR_INVALID_ARGS;
        next_rip(exit_info, vmcs);
        local_apic_state->lvt_initial_count = static_cast<uint32_t>(guest_state->rax);
        update_timer(local_apic_state, lvt_deadline(local_apic_state));
        return ZX_OK;
    case X2ApicMsr::DCR:
        if (guest_state->rax > UINT32_MAX)
            return ZX_ERR_INVALID_ARGS;
        next_rip(exit_info, vmcs);
        local_apic_state->lvt_divide_config = static_cast<uint32_t>(guest_state->rax);
        update_timer(local_apic_state, lvt_deadline(local_apic_state));
        return ZX_OK;
    case X2ApicMsr::SELF_IPI: {
        next_rip(exit_info, vmcs);
        uint32_t vector = static_cast<uint32_t>(guest_state->rax) & UINT8_MAX;
        return local_apic_state->interrupt_tracker.Interrupt(vector, nullptr);
    }
    case X2ApicMsr::ICR:
        return handle_ipi(exit_info, vmcs, guest_state, packet);
    default:
        // Issue a general protection fault for read only and unimplemented
        // registers.
        dprintf(INFO, "Unhandled x2APIC wrmsr %#lx\n", guest_state->rcx);
        return local_apic_state->interrupt_tracker.Interrupt(X86_INT_GP_FAULT, nullptr);
    }
}

static zx_status_t handle_kvm_wrmsr(const ExitInfo& exit_info, AutoVmcs* vmcs,
                                    GuestState* guest_state, LocalApicState* local_apic_state,
                                    PvClockState* pvclock,
                                    hypervisor::GuestPhysicalAddressSpace* gpas) {
    zx_paddr_t guest_paddr = BITS(guest_state->rax, 31, 0) | (BITS(guest_state->rdx, 31, 0) << 32);

    next_rip(exit_info, vmcs);
    switch (guest_state->rcx) {
    case kKvmSystemTimeMsrOld:
    case kKvmSystemTimeMsr:
        if ((guest_paddr & 1) != 0)
            return pvclock_reset_clock(pvclock, gpas, guest_paddr & ~static_cast<zx_paddr_t>(1));
        else
            pvclock_stop_clock(pvclock);
        return ZX_OK;
    case kKvmBootTimeOld:
    case kKvmBootTime:
        return pvclock_update_boot_time(gpas, guest_paddr);
    default:
        local_apic_state->interrupt_tracker.Interrupt(X86_INT_GP_FAULT, nullptr);
        return ZX_OK;
    }
}

static zx_status_t handle_wrmsr(const ExitInfo& exit_info, AutoVmcs* vmcs, GuestState* guest_state,
                                LocalApicState* local_apic_state, PvClockState* pvclock,
                                hypervisor::GuestPhysicalAddressSpace* gpas,
                                zx_port_packet* packet) {
    switch (guest_state->rcx) {
    case X86_MSR_IA32_APIC_BASE:
        if (guest_state->rdx != 0)
            return ZX_ERR_INVALID_ARGS;
        if ((guest_state->rax & ~IA32_APIC_BASE_BSP) != kLocalApicPhysBase)
            return ZX_ERR_INVALID_ARGS;
        next_rip(exit_info, vmcs);
        return ZX_OK;
    // See note in handle_rdmsr.
    case X86_MSR_IA32_MTRRCAP:
    case X86_MSR_IA32_MTRR_DEF_TYPE:
    case X86_MSR_IA32_MTRR_FIX64K_00000:
    case X86_MSR_IA32_MTRR_FIX16K_80000... X86_MSR_IA32_MTRR_FIX16K_A0000:
    case X86_MSR_IA32_MTRR_FIX4K_C0000... X86_MSR_IA32_MTRR_FIX4K_F8000:
    case X86_MSR_IA32_MTRR_PHYSBASE0... X86_MSR_IA32_MTRR_PHYSMASK9:
    case X86_MSR_IA32_BIOS_SIGN_ID:
    // From AMD64 Volume 2, Section 6.1.1: CSTAR is unused, but Linux likes to set
    // a null handler, even when not in compatibility mode. Just ignore it.
    case X86_MSR_IA32_CSTAR:
        next_rip(exit_info, vmcs);
        return ZX_OK;
    case X86_MSR_IA32_TSC_DEADLINE: {
        if ((local_apic_state->lvt_timer & LVT_TIMER_MODE_MASK) != LVT_TIMER_MODE_TSC_DEADLINE)
            return ZX_ERR_INVALID_ARGS;
        next_rip(exit_info, vmcs);
        uint64_t tsc_deadline = guest_state->rdx << 32 | (guest_state->rax & UINT32_MAX);
        update_timer(local_apic_state, ticks_to_nanos(tsc_deadline));
        return ZX_OK;
    }
    case kX2ApicMsrBase... kX2ApicMsrMax:
        return handle_apic_wrmsr(exit_info, vmcs, guest_state, local_apic_state, packet);
    case kKvmSystemTimeMsrOld:
    case kKvmSystemTimeMsr:
    case kKvmBootTimeOld:
    case kKvmBootTime:
        return handle_kvm_wrmsr(exit_info, vmcs, guest_state, local_apic_state, pvclock, gpas);
    default:
        dprintf(INFO, "Unhandled wrmsr %#lx\n", guest_state->rcx);
    // For these MSRs, we intentionally inject a general protection fault to
    // indicate to the guest that they are unsupported.
    case X86_MSR_IA32_SYSENTER_CS:
    case X86_MSR_IA32_SYSENTER_ESP:
    case X86_MSR_IA32_SYSENTER_EIP:
        return local_apic_state->interrupt_tracker.Interrupt(X86_INT_GP_FAULT, nullptr);
    }
}

/* Returns the page address for a given page table entry.
 *
 * If the page address is for a large page, we additionally calculate the offset
 * to the correct guest physical page that backs the large page.
 */
static zx_paddr_t page_addr(zx_paddr_t pt_addr, size_t level, zx_vaddr_t guest_vaddr) {
    zx_paddr_t off = 0;
    if (IS_LARGE_PAGE(pt_addr)) {
        if (level == 1) {
            off = guest_vaddr & PAGE_OFFSET_MASK_HUGE;
        } else if (level == 2) {
            off = guest_vaddr & PAGE_OFFSET_MASK_LARGE;
        }
    }
    return (pt_addr & X86_PG_FRAME) + (off & X86_PG_FRAME);
}

static zx_status_t get_page(const AutoVmcs& vmcs, hypervisor::GuestPhysicalAddressSpace* gpas,
                            zx_vaddr_t guest_vaddr, zx_paddr_t* host_paddr) {
    size_t indices[X86_PAGING_LEVELS] = {
        VADDR_TO_PML4_INDEX(guest_vaddr),
        VADDR_TO_PDP_INDEX(guest_vaddr),
        VADDR_TO_PD_INDEX(guest_vaddr),
        VADDR_TO_PT_INDEX(guest_vaddr),
    };
    zx_paddr_t pt_addr = vmcs.Read(VmcsFieldXX::GUEST_CR3);
    zx_paddr_t pa;
    for (size_t level = 0; level <= X86_PAGING_LEVELS; level++) {
        zx_status_t status = gpas->GetPage(page_addr(pt_addr, level - 1, guest_vaddr), &pa);
        if (status != ZX_OK)
            return status;
        if (level == X86_PAGING_LEVELS || IS_LARGE_PAGE(pt_addr))
            break;
        pt_entry_t* pt = static_cast<pt_entry_t*>(paddr_to_physmap(pa));
        pt_addr = pt[indices[level]];
        if (!IS_PAGE_PRESENT(pt_addr))
            return ZX_ERR_NOT_FOUND;
    }
    *host_paddr = pa;
    return ZX_OK;
}

static zx_status_t fetch_data(const AutoVmcs& vmcs, hypervisor::GuestPhysicalAddressSpace* gpas,
                              zx_vaddr_t guest_vaddr, uint8_t* data, size_t size) {
    // TODO(abdulla): Make this handle a fetch that crosses more than two pages.
    if (size > PAGE_SIZE)
        return ZX_ERR_OUT_OF_RANGE;

    zx_paddr_t pa;
    zx_status_t status = get_page(vmcs, gpas, guest_vaddr, &pa);
    if (status != ZX_OK)
        return status;

    size_t page_offset = guest_vaddr & PAGE_OFFSET_MASK_4KB;
    uint8_t* page = static_cast<uint8_t*>(paddr_to_physmap(pa));
    size_t from_page = fbl::min(size, PAGE_SIZE - page_offset);
    mandatory_memcpy(data, page + page_offset, from_page);

    // If the fetch is not split across pages, return.
    if (from_page == size)
        return ZX_OK;

    status = get_page(vmcs, gpas, guest_vaddr + size, &pa);
    if (status != ZX_OK)
        return status;

    page = static_cast<uint8_t*>(paddr_to_physmap(pa));
    mandatory_memcpy(data + from_page, page, size - from_page);
    return ZX_OK;
}

static zx_status_t handle_trap(const ExitInfo& exit_info, AutoVmcs* vmcs, bool read,
                               zx_vaddr_t guest_paddr, hypervisor::GuestPhysicalAddressSpace* gpas,
                               hypervisor::TrapMap* traps, zx_port_packet_t* packet) {
    if (exit_info.exit_instruction_length > X86_MAX_INST_LEN)
        return ZX_ERR_INTERNAL;

    hypervisor::Trap* trap;
    zx_status_t status = traps->FindTrap(ZX_GUEST_TRAP_BELL, guest_paddr, &trap);
    if (status != ZX_OK)
        return status;
    next_rip(exit_info, vmcs);

    switch (trap->kind()) {
    case ZX_GUEST_TRAP_BELL:
        if (read)
            return ZX_ERR_NOT_SUPPORTED;
        *packet = {};
        packet->key = trap->key();
        packet->type = ZX_PKT_TYPE_GUEST_BELL;
        packet->guest_bell.addr = guest_paddr;
        if (!trap->HasPort())
            return ZX_ERR_BAD_STATE;
        return trap->Queue(*packet, vmcs);
    case ZX_GUEST_TRAP_MEM:
        *packet = {};
        packet->key = trap->key();
        packet->type = ZX_PKT_TYPE_GUEST_MEM;
        packet->guest_mem.addr = guest_paddr;
        packet->guest_mem.inst_len = exit_info.exit_instruction_length & UINT8_MAX;
        status = fetch_data(*vmcs, gpas, exit_info.guest_rip, packet->guest_mem.inst_buf,
                            packet->guest_mem.inst_len);
        return status == ZX_OK ? ZX_ERR_NEXT : status;
    default:
        return ZX_ERR_BAD_STATE;
    }
}

static zx_status_t handle_ept_violation(const ExitInfo& exit_info, AutoVmcs* vmcs,
                                        hypervisor::GuestPhysicalAddressSpace* gpas,
                                        hypervisor::TrapMap* traps, zx_port_packet_t* packet) {
    EptViolationInfo ept_violation_info(exit_info.exit_qualification);
    zx_vaddr_t guest_paddr = exit_info.guest_physical_address;
    zx_status_t status = handle_trap(exit_info, vmcs, ept_violation_info.read, guest_paddr, gpas,
                                     traps, packet);
    switch (status) {
    case ZX_ERR_NOT_FOUND:
        break;
    case ZX_OK:
    default:
        return status;
    }

    // If there was no trap associated with this address and it is outside of
    // guest physical address space, return failure.
    if (guest_paddr >= gpas->size())
        return ZX_ERR_OUT_OF_RANGE;

    // By default, we mark EPT PTEs as RWX. This is so we can avoid faulting
    // again if the guest requests additional permissions, and so that we can
    // avoid use of INVEPT.
    uint pf_flags = VMM_PF_FLAG_HW_FAULT | VMM_PF_FLAG_WRITE | VMM_PF_FLAG_INSTRUCTION;
    status = vmm_guest_page_fault_handler(guest_paddr, pf_flags, gpas->aspace());
    if (status != ZX_OK) {
        dprintf(CRITICAL, "Unhandled EPT violation %#lx\n",
                exit_info.guest_physical_address);
    }
    return status;
}

static zx_status_t handle_xsetbv(const ExitInfo& exit_info, AutoVmcs* vmcs,
                                 GuestState* guest_state) {
    uint64_t guest_cr4 = vmcs->Read(VmcsFieldXX::GUEST_CR4);
    if (!(guest_cr4 & X86_CR4_OSXSAVE))
        return ZX_ERR_INVALID_ARGS;

    // We only support XCR0.
    if (guest_state->rcx != 0)
        return ZX_ERR_INVALID_ARGS;

    cpuid_leaf leaf;
    if (!x86_get_cpuid_subleaf(X86_CPUID_XSAVE, 0, &leaf))
        return ZX_ERR_INTERNAL;

    // Check that XCR0 is valid.
    uint64_t xcr0_bitmap = ((uint64_t)leaf.d << 32) | leaf.a;
    uint64_t xcr0 = (guest_state->rdx << 32) | (guest_state->rax & UINT32_MAX);
    if (~xcr0_bitmap & xcr0 ||
        // x87 state must be enabled.
        (xcr0 & X86_XSAVE_STATE_X87) != X86_XSAVE_STATE_X87 ||
        // If AVX state is enabled, SSE state must be enabled.
        (xcr0 & (X86_XSAVE_STATE_AVX | X86_XSAVE_STATE_SSE)) == X86_XSAVE_STATE_AVX)
        return ZX_ERR_INVALID_ARGS;

    guest_state->xcr0 = xcr0;
    next_rip(exit_info, vmcs);
    return ZX_OK;
}

static zx_status_t handle_pause(const ExitInfo& exit_info, AutoVmcs* vmcs) {
    next_rip(exit_info, vmcs);
    vmcs->Invalidate();
    thread_reschedule();
    return ZX_OK;
}

zx_status_t vmexit_handler(AutoVmcs* vmcs, GuestState* guest_state,
                           LocalApicState* local_apic_state, PvClockState* pvclock,
                           hypervisor::GuestPhysicalAddressSpace* gpas, hypervisor::TrapMap* traps,
                           zx_port_packet_t* packet) {
    ExitInfo exit_info(*vmcs);
    switch (exit_info.exit_reason) {
    case ExitReason::EXTERNAL_INTERRUPT:
        ktrace_vcpu(TAG_VCPU_EXIT, VCPU_EXTERNAL_INTERRUPT);
        return handle_external_interrupt(vmcs, local_apic_state);
    case ExitReason::INTERRUPT_WINDOW:
        LTRACEF("handling interrupt window\n\n");
        ktrace_vcpu(TAG_VCPU_EXIT, VCPU_INTERRUPT_WINDOW);
        return handle_interrupt_window(vmcs, local_apic_state);
    case ExitReason::CPUID:
        LTRACEF("handling CPUID\n\n");
        ktrace_vcpu(TAG_VCPU_EXIT, VCPU_CPUID);
        return handle_cpuid(exit_info, vmcs, guest_state);
    case ExitReason::HLT:
        LTRACEF("handling HLT\n\n");
        ktrace_vcpu(TAG_VCPU_EXIT, VCPU_HLT);
        return handle_hlt(exit_info, vmcs, local_apic_state);
    case ExitReason::CONTROL_REGISTER_ACCESS:
        LTRACEF("handling control-register access\n\n");
        ktrace_vcpu(TAG_VCPU_EXIT, VCPU_CONTROL_REGISTER_ACCESS);
        return handle_control_register_access(exit_info, vmcs, guest_state);
    case ExitReason::IO_INSTRUCTION:
        ktrace_vcpu(TAG_VCPU_EXIT, VCPU_IO_INSTRUCTION);
        return handle_io_instruction(exit_info, vmcs, guest_state, traps, packet);
    case ExitReason::RDMSR:
        LTRACEF("handling RDMSR %#lx\n\n", guest_state->rcx);
        ktrace_vcpu(TAG_VCPU_EXIT, VCPU_RDMSR);
        return handle_rdmsr(exit_info, vmcs, guest_state, local_apic_state);
    case ExitReason::WRMSR:
        LTRACEF("handling WRMSR %#lx\n\n", guest_state->rcx);
        ktrace_vcpu(TAG_VCPU_EXIT, VCPU_WRMSR);
        return handle_wrmsr(exit_info, vmcs, guest_state, local_apic_state, pvclock, gpas, packet);
    case ExitReason::ENTRY_FAILURE_GUEST_STATE:
    case ExitReason::ENTRY_FAILURE_MSR_LOADING:
        LTRACEF("handling VM entry failure\n\n");
        ktrace_vcpu(TAG_VCPU_EXIT, VCPU_VM_ENTRY_FAILURE);
        return ZX_ERR_BAD_STATE;
    case ExitReason::EPT_VIOLATION:
        LTRACEF("handling EPT violation\n\n");
        ktrace_vcpu(TAG_VCPU_EXIT, VCPU_EPT_VIOLATION);
        return handle_ept_violation(exit_info, vmcs, gpas, traps, packet);
    case ExitReason::XSETBV:
        LTRACEF("handling XSETBV\n\n");
        ktrace_vcpu(TAG_VCPU_EXIT, VCPU_XSETBV);
        return handle_xsetbv(exit_info, vmcs, guest_state);
    case ExitReason::PAUSE:
        LTRACEF("handling PAUSE\n\n");
        ktrace_vcpu(TAG_VCPU_EXIT, VCPU_PAUSE);
        return handle_pause(exit_info, vmcs);
    case ExitReason::EXCEPTION:
        // Currently all exceptions except NMI delivered to guest directly. NMI causes vmexit
        // and handled by host via IDT as any other interrupt/exception.
    default:
        dprintf(CRITICAL, "Unhandled VM exit %u (%s)\n", static_cast<uint32_t>(exit_info.exit_reason),
                exit_reason_name(exit_info.exit_reason));
        ktrace_vcpu(TAG_VCPU_EXIT, VCPU_UNKNOWN);
        return ZX_ERR_NOT_SUPPORTED;
    }
}
