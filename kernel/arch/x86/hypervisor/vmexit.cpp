// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <inttypes.h>
#include <string.h>
#include <trace.h>

#include <arch/hypervisor.h>
#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <arch/x86/interrupts.h>
#include <arch/x86/mmu.h>
#include <explicit-memory/bytes.h>
#include <hypervisor/guest_physical_address_space.h>
#include <kernel/auto_lock.h>
#include <vm/fault.h>
#include <vm/pmm.h>
#include <magenta/syscalls/hypervisor.h>
#include <platform/pc/timer.h>

#include "vcpu_priv.h"
#include "vmexit_priv.h"

#define LOCAL_TRACE 0

static const uint16_t kLocalApicEoi = 0x00b0;
static const uint16_t kLocalApicLvtTimer = 0x320;
static const uint64_t kLocalApicPhysBase =
    APIC_PHYS_BASE | IA32_APIC_BASE_BSP | IA32_APIC_BASE_XAPIC_ENABLE;

static const uint64_t kMiscEnableFastStrings = 1u << 0;

static const uint32_t kFirstExtendedStateComponent = 2;
static const uint32_t kLastExtendedStateComponent = 9;
// From Volume 1, Section 13.4.
static const uint32_t kXsaveLegacyRegionSize = 512;
static const uint32_t kXsaveHeaderSize = 64;

ExitInfo::ExitInfo(const AutoVmcs& vmcs) {
    exit_reason = static_cast<ExitReason>(vmcs.Read(VmcsField32::EXIT_REASON));
    exit_qualification = vmcs.Read(VmcsFieldXX::EXIT_QUALIFICATION);
    instruction_length = vmcs.Read(VmcsField32::EXIT_INSTRUCTION_LENGTH);
    guest_physical_address = vmcs.Read(VmcsField64::GUEST_PHYSICAL_ADDRESS);
    guest_rip = vmcs.Read(VmcsFieldXX::GUEST_RIP);

    if (exit_reason == ExitReason::EXTERNAL_INTERRUPT ||
        exit_reason == ExitReason::IO_INSTRUCTION)
        return;

    LTRACEF("exit reason: %#" PRIx32 "\n", static_cast<uint32_t>(exit_reason));
    LTRACEF("exit qualification: %#" PRIx64 "\n", exit_qualification);
    LTRACEF("instruction length: %#" PRIx32 "\n", instruction_length);
    LTRACEF("guest physical address: %#" PRIx64 "\n", guest_physical_address);
    LTRACEF("guest linear address: %#" PRIx64 "\n", vmcs.Read(VmcsFieldXX::GUEST_LINEAR_ADDRESS));
    LTRACEF("guest activity state: %#" PRIx32 "\n", vmcs.Read(VmcsField32::GUEST_ACTIVITY_STATE));
    LTRACEF("guest interruptibility state: %#" PRIx32 "\n",
            vmcs.Read(VmcsField32::GUEST_INTERRUPTIBILITY_STATE));
    LTRACEF("guest rip: %#" PRIx64 "\n", guest_rip);
}

IoInfo::IoInfo(uint64_t qualification) {
    access_size = static_cast<uint8_t>(BITS(qualification, 2, 0) + 1);
    input = BIT_SHIFT(qualification, 3);
    string = BIT_SHIFT(qualification, 4);
    repeat = BIT_SHIFT(qualification, 5);
    port = static_cast<uint16_t>(BITS_SHIFT(qualification, 31, 16));
}

ApicAccessInfo::ApicAccessInfo(uint64_t qualification) {
    offset = static_cast<uint16_t>(BITS(qualification, 11, 0));
    access_type = static_cast<ApicAccessType>(BITS_SHIFT(qualification, 15, 12));
}

EptViolationInfo::EptViolationInfo(uint64_t qualification) {
    // From Volume 3C, Table 27-7.
    read = BIT(qualification, 0);
    write = BIT(qualification, 1);
    instruction = BIT(qualification, 2);
    present = BITS(qualification, 5, 3);
}

static void next_rip(const ExitInfo& exit_info, AutoVmcs* vmcs) {
    vmcs->Write(VmcsFieldXX::GUEST_RIP, exit_info.guest_rip + exit_info.instruction_length);
}

/* Removes the highest priority interrupt from the bitmap, and returns it. */
static uint32_t local_apic_pop_interrupt(LocalApicState* local_apic_state) {
    // TODO(abdulla): Handle interrupt masking.
    AutoSpinLock lock(&local_apic_state->interrupt_lock);
    size_t vector = local_apic_state->interrupt_bitmap.Scan(0, kNumInterrupts, false);
    if (vector == kNumInterrupts)
        return kNumInterrupts;
    local_apic_state->interrupt_bitmap.ClearOne(vector);
    // Reverse value to get interrupt.
    return static_cast<uint32_t>(X86_MAX_INT - vector);
}

static void local_apic_pending_interrupt(LocalApicState* local_apic_state, uint32_t vector) {
    AutoSpinLock lock(&local_apic_state->interrupt_lock);
    // We reverse the value, as RawBitmapGeneric::Scan will return the
    // lowest priority interrupt, but we need the highest priority.
    local_apic_state->interrupt_bitmap.SetOne(X86_MAX_INT - vector);
}

/* Attempts to issue an interrupt from the bitmap, returning true if it did. */
static bool local_apic_issue_interrupt(AutoVmcs* vmcs, LocalApicState* local_apic_state) {
    uint32_t vector = local_apic_pop_interrupt(local_apic_state);
    if (vector == kNumInterrupts)
        return false;
    vmcs->IssueInterrupt(vector);
    return true;
}

static void local_apic_maybe_interrupt(AutoVmcs* vmcs, LocalApicState* local_apic_state) {
    uint32_t vector = local_apic_pop_interrupt(local_apic_state);
    if (vector == kNumInterrupts)
        return;

    if (vmcs->Read(VmcsFieldXX::GUEST_RFLAGS) & X86_FLAGS_IF) {
        // If interrupts are enabled, we inject an interrupt.
        vmcs->IssueInterrupt(vector);
    } else {
        local_apic_pending_interrupt(local_apic_state, vector);
        // If interrupts are disabled, we set VM exit on interrupt enable.
        vmcs->InterruptWindowExiting(true);
    }
}

/* Sets the given interrupt in the bitmap and signals waiters, returning true if
 * a waiter was signaled.
 */
bool local_apic_signal_interrupt(LocalApicState* local_apic_state, uint32_t vector,
                                 bool reschedule) {
    local_apic_pending_interrupt(local_apic_state, vector);
    // TODO(abdulla): We can skip this check if an interrupt is pending, as we
    // would have already signaled. However, we should be careful with locking.
    return event_signal(&local_apic_state->event, reschedule) > 0;
}

static mx_status_t handle_external_interrupt(AutoVmcs* vmcs, LocalApicState* local_apic_state) {
    vmcs->InterruptibleReload();
    local_apic_maybe_interrupt(vmcs, local_apic_state);
    return MX_OK;
}

static mx_status_t handle_interrupt_window(AutoVmcs* vmcs, LocalApicState* local_apic_state) {
    vmcs->InterruptWindowExiting(false);
    local_apic_issue_interrupt(vmcs, local_apic_state);
    return MX_OK;
}

// From Volume 2, Section 3.2, Table 3-8  "Processor Extended State Enumeration
// Main Leaf (EAX = 0DH, ECX = 0)".
//
// Bits 31-00: Maximum size (bytes, from the beginning of the XSAVE/XRSTOR save
// area) required by enabled features in XCR0. May be different than ECX if some
// features at the end of the XSAVE save area are not enabled.
static mx_status_t compute_xsave_size(uint64_t guest_xcr0, uint32_t* xsave_size) {
    *xsave_size = kXsaveLegacyRegionSize + kXsaveHeaderSize;
    for (uint32_t i = kFirstExtendedStateComponent; i <= kLastExtendedStateComponent; ++i) {
        cpuid_leaf leaf;
        if (!(guest_xcr0 & (1 << i)))
            continue;
        if (!x86_get_cpuid_subleaf(X86_CPUID_XSAVE, i, &leaf))
            return MX_ERR_INTERNAL;
        if (leaf.a == 0 && leaf.b == 0 && leaf.c == 0 && leaf.d == 0)
            continue;
        const uint32_t component_offset = leaf.b;
        const uint32_t component_size = leaf.a;
        *xsave_size = component_offset + component_size;
    }
    return MX_OK;
}

static mx_status_t handle_cpuid(const ExitInfo& exit_info, AutoVmcs* vmcs,
                                GuestState* guest_state) {
    const uint64_t leaf = guest_state->rax;
    const uint64_t subleaf = guest_state->rcx;

    switch (leaf) {
    case X86_CPUID_BASE:
    case X86_CPUID_EXT_BASE:
        next_rip(exit_info, vmcs);
        cpuid((uint32_t)guest_state->rax,
              (uint32_t*)&guest_state->rax, (uint32_t*)&guest_state->rbx,
              (uint32_t*)&guest_state->rcx, (uint32_t*)&guest_state->rdx);
        return MX_OK;
    case X86_CPUID_BASE + 1 ... MAX_SUPPORTED_CPUID:
    case X86_CPUID_EXT_BASE + 1 ... MAX_SUPPORTED_CPUID_EXT:
        next_rip(exit_info, vmcs);
        cpuid_c((uint32_t)guest_state->rax, (uint32_t)guest_state->rcx,
                (uint32_t*)&guest_state->rax, (uint32_t*)&guest_state->rbx,
                (uint32_t*)&guest_state->rcx, (uint32_t*)&guest_state->rdx);
        switch (leaf) {
        case X86_CPUID_MODEL_FEATURES:
            // Enable the hypervisor bit.
            guest_state->rcx |= 1u << X86_FEATURE_HYPERVISOR.bit;
            // Disable the VMX bit.
            guest_state->rcx &= ~(1u << X86_FEATURE_VMX.bit);
            // Disable the PDCM bit.
            guest_state->rcx &= ~(1u << X86_FEATURE_PDCM.bit);
            // Disable the x2APIC bit.
            guest_state->rcx &= ~(1u << X86_FEATURE_X2APIC.bit);
            // Disable the Thermal Monitor bit.
            guest_state->rdx &= ~(1u << X86_FEATURE_TM.bit);
            break;
        case X86_CPUID_XSAVE:
            if (subleaf == 0) {
                uint32_t xsave_size = 0;
                mx_status_t status = compute_xsave_size(guest_state->xcr0, &xsave_size);
                if (status != MX_OK)
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
        case X86_CPUID_EXTENDED_FEATURE_FLAGS:
            // Disable the Processor Trace bit.
            guest_state->rbx &= ~(1u << X86_FEATURE_PT.bit);
            break;
        }
        return MX_OK;
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

static mx_status_t handle_hlt(const ExitInfo& exit_info, AutoVmcs* vmcs,
                              LocalApicState* local_apic_state) {
    do {
        mx_status_t status = event_wait_deadline(&local_apic_state->event, INFINITE_TIME, true);
        vmcs->Reload();
        if (status != MX_OK)
            return MX_ERR_CANCELED;
    } while (!local_apic_issue_interrupt(vmcs, local_apic_state));
    next_rip(exit_info, vmcs);
    return MX_OK;
}

static mx_status_t handle_io_instruction(const ExitInfo& exit_info, AutoVmcs* vmcs,
                                         GuestState* guest_state, PacketMux& mux,
                                         mx_port_packet_t* packet) {
    IoInfo io_info(exit_info.exit_qualification);
    if (io_info.string || io_info.repeat)
        return MX_ERR_NOT_SUPPORTED;
    next_rip(exit_info, vmcs);

    memset(packet, 0, sizeof(*packet));
    packet->type = MX_PKT_TYPE_GUEST_IO;
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
        mx_status_t status = mux.Queue(packet->guest_io.port, *packet, vmcs);
        // If there was no FIFO to handle the trap, then we should return to
        // user-space. Otherwise, return the status of the FIFO write.
        if (status != MX_ERR_NOT_FOUND)
            return status;
    }

    return MX_ERR_NEXT;
}

static mx_status_t handle_rdmsr(const ExitInfo& exit_info, AutoVmcs* vmcs,
                                GuestState* guest_state) {
    switch (guest_state->rcx) {
    // Guests can't control most perf/power/metrics. We typically disable them through
    // CPUID leaves, but for these MSRs Linux assumes that they work based on CPU version.
    // If we fault, Linux will detect it and ignore them.
    case X86_MSR_IA32_PPERF:
    case X86_MSR_IA32_RAPL_POWER_UNIT:
    case X86_MSR_IA32_SMI_COUNT:
    case X86_MSR_IA32_TEMPERATURE_TARGET:
        vmcs->IssueInterrupt(X86_INT_GP_FAULT);
        return MX_OK;
    case X86_MSR_IA32_APIC_BASE:
        next_rip(exit_info, vmcs);
        guest_state->rax = kLocalApicPhysBase;
        guest_state->rdx = 0;
        return MX_OK;
    // From Volume 4, Section 2.1, Table 2-2: For now, only enable fast strings.
    case X86_MSR_IA32_MISC_ENABLE:
        next_rip(exit_info, vmcs);
        guest_state->rax = read_msr(X86_MSR_IA32_MISC_ENABLE) & kMiscEnableFastStrings;
        guest_state->rdx = 0;
        return MX_OK;
    // From Volume 3, Section 28.2.6.2: The MTRRs have no effect on the memory
    // type used for an access to a guest-physical address.
    case X86_MSR_IA32_MTRRCAP:
    case X86_MSR_IA32_MTRR_DEF_TYPE:
    case X86_MSR_IA32_MTRR_FIX64K_00000:
    case X86_MSR_IA32_MTRR_FIX16K_80000 ... X86_MSR_IA32_MTRR_FIX16K_A0000:
    case X86_MSR_IA32_MTRR_FIX4K_C0000 ... X86_MSR_IA32_MTRR_FIX4K_F8000:
    case X86_MSR_IA32_MTRR_PHYSBASE0 ... X86_MSR_IA32_MTRR_PHYSMASK9:
    // From Volume 3, Section 9.11.4: For now, 0.
    case X86_MSR_IA32_PLATFORM_ID:
    // From Volume 3, Section 9.11.7: 0 indicates no microcode update is loaded.
    case X86_MSR_IA32_BIOS_SIGN_ID:
    // From Volume 3, Section 15.3.1: 0 indicates that our machine has no
    // checking capabilities.
    case X86_MSR_IA32_MCG_CAP:
    case X86_MSR_IA32_MCG_STATUS:
        next_rip(exit_info, vmcs);
        guest_state->rax = 0;
        guest_state->rdx = 0;
        return MX_OK;
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

static uint32_t* apic_reg(LocalApicState* local_apic_state, uint16_t reg) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(local_apic_state->apic_addr);
    return reinterpret_cast<uint32_t*>(addr + reg);
}

static handler_return deadline_callback(timer_t* timer, lk_time_t now, void* arg) {
    LocalApicState* local_apic_state = static_cast<LocalApicState*>(arg);
    uint32_t* lvt_timer = apic_reg(local_apic_state, kLocalApicLvtTimer);
    uint8_t vector = *lvt_timer & LVT_TIMER_VECTOR_MASK;
    local_apic_signal_interrupt(local_apic_state, vector, false);
    return INT_NO_RESCHEDULE;
}

static mx_status_t handle_wrmsr(const ExitInfo& exit_info, AutoVmcs* vmcs, GuestState* guest_state,
                                LocalApicState* local_apic_state) {
    switch (guest_state->rcx) {
    case X86_MSR_IA32_APIC_BASE:
        if (guest_state->rax != kLocalApicPhysBase || guest_state->rdx != 0)
            return MX_ERR_INVALID_ARGS;
        next_rip(exit_info, vmcs);
        return MX_OK;
    // See note in handle_rdmsr.
    case X86_MSR_IA32_MTRRCAP:
    case X86_MSR_IA32_MTRR_DEF_TYPE:
    case X86_MSR_IA32_MTRR_FIX64K_00000:
    case X86_MSR_IA32_MTRR_FIX16K_80000 ... X86_MSR_IA32_MTRR_FIX16K_A0000:
    case X86_MSR_IA32_MTRR_FIX4K_C0000 ... X86_MSR_IA32_MTRR_FIX4K_F8000:
    case X86_MSR_IA32_MTRR_PHYSBASE0 ... X86_MSR_IA32_MTRR_PHYSMASK9:
    case X86_MSR_IA32_BIOS_SIGN_ID:
    // From AMD64 Volume 2, Section 6.1.1: CSTAR is unused, but Linux likes to set
    // a null handler, even when not in compatibility mode. Just ignore it.
    case X86_MSR_IA32_CSTAR:
        next_rip(exit_info, vmcs);
        return MX_OK;
    // Legacy syscall MSRs are unused and we clear them in the VMCS.
    // Allow guests to clear them too. Anything else is an error.
    case X86_MSR_IA32_SYSENTER_CS:
    case X86_MSR_IA32_SYSENTER_ESP:
    case X86_MSR_IA32_SYSENTER_EIP:
        if (guest_state->rax != 0 || guest_state->rdx != 0)
            return MX_ERR_NOT_SUPPORTED;
        next_rip(exit_info, vmcs);
        return MX_OK;
    case X86_MSR_IA32_TSC_DEADLINE: {
        uint32_t* reg = apic_reg(local_apic_state, kLocalApicLvtTimer);
        if ((*reg & LVT_TIMER_MODE_MASK) != LVT_TIMER_MODE_TSC_DEADLINE)
            return MX_ERR_INVALID_ARGS;
        next_rip(exit_info, vmcs);
        timer_cancel(&local_apic_state->timer);
        uint64_t tsc_deadline = guest_state->rdx << 32 | (guest_state->rax & UINT32_MAX);
        if (tsc_deadline > 0) {
            lk_time_t deadline = ticks_to_nanos(tsc_deadline);
            timer_set_oneshot(&local_apic_state->timer, deadline, deadline_callback,
                              local_apic_state);
        }
        return MX_OK;
    }
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

/* Returns the page address for a given page table entry.
 *
 * If the page address is for a large page, we additionally calculate the offset
 * to the correct guest physical page that backs the large page.
 */
static paddr_t page_addr(paddr_t pt_addr, size_t level, vaddr_t guest_vaddr) {
    paddr_t off = 0;
    if (IS_LARGE_PAGE(pt_addr)) {
        if (level == 1) {
            off = guest_vaddr & PAGE_OFFSET_MASK_HUGE;
        } else if (level == 2) {
            off = guest_vaddr & PAGE_OFFSET_MASK_LARGE;
        }
    }
    return (pt_addr & X86_PG_FRAME) + (off & X86_PG_FRAME);
}

static mx_status_t get_page(const AutoVmcs& vmcs, GuestPhysicalAddressSpace* gpas,
                            vaddr_t guest_vaddr, paddr_t* host_paddr) {
    size_t indices[X86_PAGING_LEVELS] = {
        VADDR_TO_PML4_INDEX(guest_vaddr),
        VADDR_TO_PDP_INDEX(guest_vaddr),
        VADDR_TO_PD_INDEX(guest_vaddr),
        VADDR_TO_PT_INDEX(guest_vaddr),
    };
    paddr_t pt_addr = vmcs.Read(VmcsFieldXX::GUEST_CR3);
    paddr_t pa;
    for (size_t level = 0; level <= X86_PAGING_LEVELS; level++) {
        mx_status_t status = gpas->GetPage(page_addr(pt_addr, level - 1, guest_vaddr), &pa);
        if (status != MX_OK)
            return status;
        if (level == X86_PAGING_LEVELS || IS_LARGE_PAGE(pt_addr))
            break;
        pt_entry_t* pt = static_cast<pt_entry_t*>(paddr_to_kvaddr(pa));
        pt_addr = pt[indices[level]];
        if (!IS_PAGE_PRESENT(pt_addr))
            return MX_ERR_NOT_FOUND;
    }
    *host_paddr = pa;
    return MX_OK;
}

static mx_status_t fetch_data(const AutoVmcs& vmcs, GuestPhysicalAddressSpace* gpas,
                              vaddr_t guest_vaddr, uint8_t* data, size_t size) {
    // TODO(abdulla): Make this handle a fetch that crosses more than two pages.
    if (size > PAGE_SIZE)
        return MX_ERR_OUT_OF_RANGE;

    paddr_t pa;
    mx_status_t status = get_page(vmcs, gpas, guest_vaddr, &pa);
    if (status != MX_OK)
        return status;

    size_t page_offset = guest_vaddr & PAGE_OFFSET_MASK_4KB;
    uint8_t* page = static_cast<uint8_t*>(paddr_to_kvaddr(pa));
    size_t from_page = fbl::min(size, PAGE_SIZE - page_offset);
    mandatory_memcpy(data, page + page_offset, from_page);

    // If the fetch is not split across pages, return.
    if (from_page == size)
        return MX_OK;

    status = get_page(vmcs, gpas, guest_vaddr + size, &pa);
    if (status != MX_OK)
        return status;

    page = static_cast<uint8_t*>(paddr_to_kvaddr(pa));
    mandatory_memcpy(data + from_page, page, size - from_page);
    return MX_OK;
}

static mx_status_t handle_memory(const ExitInfo& exit_info, AutoVmcs* vmcs, vaddr_t guest_paddr,
                                 GuestPhysicalAddressSpace* gpas, mx_port_packet_t* packet) {
    if (exit_info.instruction_length > X86_MAX_INST_LEN)
        return MX_ERR_INTERNAL;

    memset(packet, 0, sizeof(*packet));
    packet->type = MX_PKT_TYPE_GUEST_MEM;
    packet->guest_mem.addr = guest_paddr;
    packet->guest_mem.inst_len = exit_info.instruction_length & UINT8_MAX;
    mx_status_t status = fetch_data(*vmcs, gpas, exit_info.guest_rip, packet->guest_mem.inst_buf,
                                    packet->guest_mem.inst_len);
    if (status != MX_OK)
        return status;

    next_rip(exit_info, vmcs);
    return MX_ERR_NEXT;
}

static mx_status_t handle_apic_access(const ExitInfo& exit_info, AutoVmcs* vmcs,
                                      LocalApicState* local_apic_state,
                                      GuestPhysicalAddressSpace* gpas, mx_port_packet_t* packet) {
    ApicAccessInfo apic_access_info(exit_info.exit_qualification);
    switch (apic_access_info.access_type) {
    default:
        return MX_ERR_NOT_SUPPORTED;
    case ApicAccessType::LINEAR_ACCESS_WRITE:
        if (apic_access_info.offset == kLocalApicEoi) {
            // When we observe an EOI, we issue any pending interrupts. This is
            // not architecture-accurate, but works for the virtual machine.
            local_apic_maybe_interrupt(vmcs, local_apic_state);
            next_rip(exit_info, vmcs);
            return MX_OK;
        }
    /* fallthrough */
    case ApicAccessType::LINEAR_ACCESS_READ:
        vaddr_t guest_paddr = APIC_PHYS_BASE + apic_access_info.offset;
        return handle_memory(exit_info, vmcs, guest_paddr, gpas, packet);
    }
}

static mx_status_t handle_ept_violation(const ExitInfo& exit_info, AutoVmcs* vmcs,
                                        GuestPhysicalAddressSpace* gpas, mx_port_packet_t* packet) {
    vaddr_t guest_paddr = exit_info.guest_physical_address;
    EptViolationInfo ept_violation_info(exit_info.exit_qualification);

    uint pf_flags = VMM_PF_FLAG_HW_FAULT;
    if (ept_violation_info.write)
        pf_flags |= VMM_PF_FLAG_WRITE;
    if (ept_violation_info.instruction)
        pf_flags |= VMM_PF_FLAG_INSTRUCTION;
    if (!ept_violation_info.present)
        pf_flags |= VMM_PF_FLAG_NOT_PRESENT;

    // TODO(tjdetwiler): We'll always call the page fault handler for addresses
    // that userspace wants to handle (ex: MMIO). We should be able to optimize
    // for this use case.
    mx_status_t result = vmm_guest_page_fault_handler(guest_paddr, pf_flags, gpas->aspace());
    if (result != MX_ERR_NOT_FOUND)
        return result;

    return handle_memory(exit_info, vmcs, guest_paddr, gpas, packet);
}

static mx_status_t handle_xsetbv(const ExitInfo& exit_info, AutoVmcs* vmcs,
                                 GuestState* guest_state) {
    uint64_t guest_cr4 = vmcs->Read(VmcsFieldXX::GUEST_CR4);
    if (!(guest_cr4 & X86_CR4_OSXSAVE))
        return MX_ERR_INVALID_ARGS;

    // We only support XCR0.
    if (guest_state->rcx != 0)
        return MX_ERR_INVALID_ARGS;

    cpuid_leaf leaf;
    if (!x86_get_cpuid_subleaf(X86_CPUID_XSAVE, 0, &leaf))
        return MX_ERR_INTERNAL;

    // Check that XCR0 is valid.
    uint64_t xcr0_bitmap = ((uint64_t)leaf.d << 32) | leaf.a;
    uint64_t xcr0 = (guest_state->rdx << 32) | (guest_state->rax & UINT32_MAX);
    if (~xcr0_bitmap & xcr0 ||
        // x87 state must be enabled.
        (xcr0 & X86_XSAVE_STATE_X87) != X86_XSAVE_STATE_X87 ||
        // If AVX state is enabled, SSE state must be enabled.
        (xcr0 & (X86_XSAVE_STATE_AVX | X86_XSAVE_STATE_SSE)) == X86_XSAVE_STATE_AVX)
        return MX_ERR_INVALID_ARGS;

    guest_state->xcr0 = xcr0;
    next_rip(exit_info, vmcs);
    return MX_OK;
}

mx_status_t vmexit_handler(AutoVmcs* vmcs, GuestState* guest_state,
                           LocalApicState* local_apic_state, GuestPhysicalAddressSpace* gpas,
                           PacketMux& mux, mx_port_packet_t* packet) {
    ExitInfo exit_info(*vmcs);

    switch (exit_info.exit_reason) {
    case ExitReason::EXTERNAL_INTERRUPT:
        return handle_external_interrupt(vmcs, local_apic_state);
    case ExitReason::INTERRUPT_WINDOW:
        LTRACEF("handling interrupt window\n\n");
        return handle_interrupt_window(vmcs, local_apic_state);
    case ExitReason::CPUID:
        LTRACEF("handling CPUID instruction\n\n");
        return handle_cpuid(exit_info, vmcs, guest_state);
    case ExitReason::HLT:
        LTRACEF("handling HLT instruction\n\n");
        return handle_hlt(exit_info, vmcs, local_apic_state);
    case ExitReason::IO_INSTRUCTION:
        return handle_io_instruction(exit_info, vmcs, guest_state, mux, packet);
    case ExitReason::RDMSR:
        LTRACEF("handling RDMSR instruction %#" PRIx64 "\n\n", guest_state->rcx);
        return handle_rdmsr(exit_info, vmcs, guest_state);
    case ExitReason::WRMSR:
        LTRACEF("handling WRMSR instruction %#" PRIx64 "\n\n", guest_state->rcx);
        return handle_wrmsr(exit_info, vmcs, guest_state, local_apic_state);
    case ExitReason::ENTRY_FAILURE_GUEST_STATE:
    case ExitReason::ENTRY_FAILURE_MSR_LOADING:
        LTRACEF("handling VM entry failure\n\n");
        return MX_ERR_BAD_STATE;
    case ExitReason::APIC_ACCESS:
        LTRACEF("handling APIC access\n\n");
        return handle_apic_access(exit_info, vmcs, local_apic_state, gpas, packet);
    case ExitReason::EPT_VIOLATION:
        LTRACEF("handling EPT violation\n\n");
        return handle_ept_violation(exit_info, vmcs, gpas, packet);
    case ExitReason::XSETBV:
        LTRACEF("handling XSETBV instruction\n\n");
        return handle_xsetbv(exit_info, vmcs, guest_state);
    default:
        LTRACEF("unhandled VM exit %u\n\n", static_cast<uint32_t>(exit_info.exit_reason));
        return MX_ERR_NOT_SUPPORTED;
    }
}
