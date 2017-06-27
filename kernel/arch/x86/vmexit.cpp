// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <platform.h>
#include <string.h>
#include <trace.h>

#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <arch/x86/interrupts.h>
#include <arch/x86/mmu.h>
#include <hypervisor/guest_physical_address_space.h>
#include <kernel/sched.h>
#include <kernel/timer.h>
#include <kernel/vm/pmm.h>
#include <mxtl/algorithm.h>
#include <platform/pc/timer.h>

#include "hypervisor_priv.h"
#include "vmexit_priv.h"

#if WITH_LIB_MAGENTA
#include <magenta/fifo_dispatcher.h>
#include <magenta/syscalls/hypervisor.h>

static const uint16_t kLocalApicEoi = 0x00b0;
#endif // WITH_LIB_MAGENTA

#define LOCAL_TRACE 0

static const uint64_t kLocalApicPhysBase =
    APIC_PHYS_BASE | IA32_APIC_BASE_BSP | IA32_APIC_BASE_XAPIC_ENABLE;
static const uint16_t kLocalApicLvtTimer = 0x320;

static const uint64_t kMiscEnableFastStrings = 1u << 0;

static const uint32_t kInterruptInfoDeliverErrorCode = 1u << 11;
static const uint32_t kInterruptInfoValid = 1u << 31;
static const uint64_t kInvalidErrorCode = UINT64_MAX;
static const uint32_t kFirstExtendedStateComponent = 2;
static const uint32_t kLastExtendedStateComponent = 9;
// From Volume 1, Section 13.4.
static const uint32_t kXsaveLegacyRegionSize = 512;
static const uint32_t kXsaveHeaderSize = 64;

ExitInfo::ExitInfo() {
    exit_reason = static_cast<ExitReason>(vmcs_read(VmcsField32::EXIT_REASON));
    exit_qualification = vmcs_read(VmcsFieldXX::EXIT_QUALIFICATION);
    instruction_length = vmcs_read(VmcsField32::EXIT_INSTRUCTION_LENGTH);
    guest_physical_address = vmcs_read(VmcsField64::GUEST_PHYSICAL_ADDRESS);
    guest_rip = vmcs_read(VmcsFieldXX::GUEST_RIP);

    if (exit_reason == ExitReason::EXTERNAL_INTERRUPT ||
        exit_reason == ExitReason::IO_INSTRUCTION)
        return;

    LTRACEF("exit reason: %#" PRIx32 "\n", static_cast<uint32_t>(exit_reason));
    LTRACEF("exit qualification: %#" PRIx64 "\n", exit_qualification);
    LTRACEF("instruction length: %#" PRIx32 "\n", instruction_length);
    LTRACEF("guest physical address: %#" PRIx64 "\n", guest_physical_address);
    LTRACEF("guest linear address: %#" PRIx64 "\n", vmcs_read(VmcsFieldXX::GUEST_LINEAR_ADDRESS));
    LTRACEF("guest activity state: %#" PRIx32 "\n", vmcs_read(VmcsField32::GUEST_ACTIVITY_STATE));
    LTRACEF("guest interruptibility state: %#" PRIx32 "\n",
        vmcs_read(VmcsField32::GUEST_INTERRUPTIBILITY_STATE));
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

static void next_rip(const ExitInfo& exit_info) {
    vmcs_write(VmcsFieldXX::GUEST_RIP, exit_info.guest_rip + exit_info.instruction_length);
}

static void issue_interrupt(uint16_t interrupt, uint64_t error_code, InterruptionType type) {
    uint32_t interrupt_info = kInterruptInfoValid | static_cast<uint32_t>(type) << 8 | interrupt;
    if (error_code != kInvalidErrorCode) {
        interrupt_info |= kInterruptInfoDeliverErrorCode;
        vmcs_write(VmcsField32::ENTRY_EXCEPTION_ERROR_CODE, error_code & UINT32_MAX);
    }
    vmcs_write(VmcsField32::ENTRY_INTERRUPTION_INFORMATION, interrupt_info);
}

/* Removes the highest priority interrupt from the bitmap, and returns it. */
static uint16_t local_apic_pop_interrupt(LocalApicState* local_apic_state) {
    // TODO(abdulla): Handle interrupt masking.
    AutoSpinLock lock(local_apic_state->interrupt_lock);
    size_t interrupt = local_apic_state->interrupt_bitmap.Scan(0, kNumInterrupts, false);
    if (interrupt == kNumInterrupts)
        return kNumInterrupts;
    local_apic_state->interrupt_bitmap.ClearOne(interrupt);
    // Reverse value to get interrupt.
    return static_cast<uint16_t>(X86_MAX_INT - interrupt);
}

static void local_apic_pending_interrupt(LocalApicState* local_apic_state, uint16_t interrupt) {
    AutoSpinLock lock(local_apic_state->interrupt_lock);
    // We reverse the value, as RawBitmapGeneric::Scan will return the
    // lowest priority interrupt, but we need the highest priority.
    local_apic_state->interrupt_bitmap.SetOne(X86_MAX_INT - interrupt);
}

/* Attempts to issue an interrupt from the bitmap, returning true if it did. */
static bool local_apic_issue_interrupt(LocalApicState* local_apic_state) {
    uint16_t interrupt = local_apic_pop_interrupt(local_apic_state);
    if (interrupt == kNumInterrupts)
        return false;
    issue_interrupt(interrupt, kInvalidErrorCode, InterruptionType::EXTERNAL_INTERRUPT);
    return true;
}

static void local_apic_maybe_interrupt(LocalApicState* local_apic_state) {
    uint16_t interrupt = local_apic_pop_interrupt(local_apic_state);
    if (interrupt == kNumInterrupts)
        return;

    if (vmcs_read(VmcsFieldXX::GUEST_RFLAGS) & X86_FLAGS_IF) {
        // If interrupts are enabled, we inject an interrupt.
        issue_interrupt(interrupt, kInvalidErrorCode, InterruptionType::EXTERNAL_INTERRUPT);
    } else {
        local_apic_pending_interrupt(local_apic_state, interrupt);
        // If interrupts are disabled, we set VM exit on interrupt enable.
        interrupt_window_exiting(true);
    }
}

/* Sets the given interrupt in the bitmap and signals waiters, returning true if
 * a waiter was signaled.
 */
bool local_apic_signal_interrupt(LocalApicState* local_apic_state, uint8_t interrupt,
                                 bool reschedule) {
    local_apic_pending_interrupt(local_apic_state, interrupt);
    // TODO(abdulla): We can skip this check if an interrupt is pending, as we
    // would have already signaled. However, we should be careful with locking.
    return event_signal(&local_apic_state->event, reschedule) > 0;
}

void interrupt_window_exiting(bool enable) {
    uint32_t controls = vmcs_read(VmcsField32::PROCBASED_CTLS);
    if (enable) {
        controls |= PROCBASED_CTLS_INT_WINDOW_EXITING;
    } else {
        controls &= ~PROCBASED_CTLS_INT_WINDOW_EXITING;
    }
    vmcs_write(VmcsField32::PROCBASED_CTLS, controls);
}

static status_t handle_external_interrupt(AutoVmcsLoad* vmcs_load,
                                          LocalApicState* local_apic_state) {
    vmcs_load->reload(true);
    local_apic_maybe_interrupt(local_apic_state);
    return MX_OK;
}

static status_t handle_interrupt_window(LocalApicState* local_apic_state) {
    interrupt_window_exiting(false);
    local_apic_issue_interrupt(local_apic_state);
    return MX_OK;
}

// From Volume 2, Section 3.2, Table 3-8  "Processor Extended State Enumeration
// Main Leaf (EAX = 0DH, ECX = 0)".
//
// Bits 31-00: Maximum size (bytes, from the beginning of the XSAVE/XRSTOR save
// area) required by enabled features in XCR0. May be different than ECX if some
// features at the end of the XSAVE save area are not enabled.
status_t compute_xsave_size(uint64_t guest_xcr0, uint32_t* xsave_size) {
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
        return MX_OK;
    case X86_CPUID_BASE + 1 ... MAX_SUPPORTED_CPUID:
    case X86_CPUID_EXT_BASE + 1 ... MAX_SUPPORTED_CPUID_EXT:
        next_rip(exit_info);
        cpuid_c((uint32_t)guest_state->rax, (uint32_t)guest_state->rcx,
                (uint32_t*)&guest_state->rax, (uint32_t*)&guest_state->rbx,
                (uint32_t*)&guest_state->rcx, (uint32_t*)&guest_state->rdx);
        if (leaf == X86_CPUID_MODEL_FEATURES) {
            // Enable the hypervisor bit.
            guest_state->rcx |= 1u << X86_FEATURE_HYPERVISOR.bit;
            // Disable the VMX bit.
            guest_state->rcx &= ~(1u << X86_FEATURE_VMX.bit);
            // Disable the x2APIC bit.
            guest_state->rcx &= ~(1u << X86_FEATURE_X2APIC.bit);
        }
        if (leaf == X86_CPUID_XSAVE) {
            if (subleaf == 0) {
                uint32_t xsave_size = 0;
                status_t status = compute_xsave_size(guest_state->xcr0, &xsave_size);
                if (status != MX_OK)
                    return status;
                guest_state->rbx = xsave_size;
            } else if (subleaf == 1) {
                guest_state->rax &= ~(1u << 3);
            }
        }
        return MX_OK;
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

static status_t handle_hlt(const ExitInfo& exit_info, AutoVmcsLoad* vmcs_load,
                           LocalApicState* local_apic_state) {
    // TODO(abdulla): Use an interruptible sleep here, so that we can:
    // a) Continue to deliver interrupts to the guest.
    // b) Kill the hypervisor while a guest is halted.
    do {
        event_wait(&local_apic_state->event);
        vmcs_load->reload(false);
    } while (!local_apic_issue_interrupt(local_apic_state));
    next_rip(exit_info);
    return MX_OK;
}

#if WITH_LIB_MAGENTA
class FifoStateObserver : public StateObserver {
public:
    FifoStateObserver(AutoVmcsLoad* vmcs_load, mx_signals_t watched_signals)
        : vmcs_load_(vmcs_load), watched_signals_(watched_signals) {
        event_init(&event_, false, 0);
    }
    status_t Wait(StateTracker* state_tracker) {
        state_tracker->AddObserver(this, nullptr);
        status_t status = event_wait(&event_);
        vmcs_load_->reload(false);
        state_tracker->RemoveObserver(this);
        return status;
    }
private:
    AutoVmcsLoad* vmcs_load_;
    mx_signals_t watched_signals_;
    event_t event_;
    virtual Flags OnInitialize(mx_signals_t initial_state, const CountInfo* cinfo) {
        return 0;
    }
    virtual Flags OnStateChange(mx_signals_t new_state) {
        if (new_state & watched_signals_)
            event_signal(&event_, false);
        return 0;
    }
    virtual Flags OnCancel(Handle* handle) {
        return 0;
    }
};

static status_t packet_wait(AutoVmcsLoad* vmcs_load, StateTracker* state_tracker,
                            mx_signals_t signals) {
    if (state_tracker->GetSignalsState() & signals)
        return MX_OK;
    // TODO(abdulla): Add stats to keep track of waits.
    FifoStateObserver state_observer(vmcs_load, signals | MX_FIFO_PEER_CLOSED);
    status_t status = state_observer.Wait(state_tracker);
    if (status != MX_OK)
        return status;
    return state_tracker->GetSignalsState() & MX_FIFO_PEER_CLOSED ? MX_ERR_PEER_CLOSED : MX_OK;
}

static status_t packet_write(AutoVmcsLoad* vmcs_load, FifoDispatcher* ctl_fifo,
                             const mx_guest_packet_t& packet) {
    status_t status = packet_wait(vmcs_load, ctl_fifo->get_state_tracker(), MX_FIFO_WRITABLE);
    if (status != MX_OK)
        return status;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(&packet);
    uint32_t actual;
    status = ctl_fifo->Write(data, sizeof(mx_guest_packet_t), &actual);
    if (status != MX_OK)
        return status;
    return actual != 1 ? MX_ERR_IO_DATA_INTEGRITY : MX_OK;
}

static status_t packet_read(AutoVmcsLoad* vmcs_load, FifoDispatcher* ctl_fifo,
                            mx_guest_packet_t* packet) {
    status_t status = packet_wait(vmcs_load, ctl_fifo->get_state_tracker(), MX_FIFO_READABLE);
    if (status != MX_OK)
        return status;
    uint8_t* data = reinterpret_cast<uint8_t*>(packet);
    uint32_t actual;
    status = ctl_fifo->Read(data, sizeof(mx_guest_packet_t), &actual);
    if (status != MX_OK)
        return status;
    return actual != 1 ? MX_ERR_IO_DATA_INTEGRITY : MX_OK;
}
#endif // WITH_LIB_MAGENTA

static status_t handle_io_instruction(const ExitInfo& exit_info, AutoVmcsLoad* vmcs_load,
                                      GuestState* guest_state, FifoDispatcher* ctl_fifo) {
    next_rip(exit_info);
#if WITH_LIB_MAGENTA
    IoInfo io_info(exit_info.exit_qualification);
    if (io_info.string || io_info.repeat)
        return MX_ERR_NOT_SUPPORTED;
    mx_guest_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    if (!io_info.input) {
        packet.type = MX_GUEST_PKT_TYPE_PORT_OUT;
        packet.port_out.access_size = io_info.access_size;
        packet.port_out.port = io_info.port;
        memcpy(packet.port_out.data, &guest_state->rax, io_info.access_size);
        return packet_write(vmcs_load, ctl_fifo, packet);
    }
    packet.type = MX_GUEST_PKT_TYPE_PORT_IN;
    packet.port_in.port = io_info.port;
    packet.port_in.access_size = io_info.access_size;
    mx_status_t status = packet_write(vmcs_load, ctl_fifo, packet);
    if (status != MX_OK)
        return status;
    status = packet_read(vmcs_load, ctl_fifo, &packet);
    if (status != MX_OK)
        return status;
    if (packet.type != MX_GUEST_PKT_TYPE_PORT_IN)
        return MX_ERR_INVALID_ARGS;
    // From Volume 1, Section 3.4.1.1: 32-bit operands generate a 32-bit result,
    // zero-extended to a 64-bit result in the destination general-purpose
    // register.
    if (io_info.access_size == 4)
        guest_state->rax = 0;
    memcpy(&guest_state->rax, packet.port_in_ret.data, io_info.access_size);
    return MX_OK;
#else // WITH_LIB_MAGENTA
    return MX_ERR_NOT_SUPPORTED;
#endif // WITH_LIB_MAGENTA
}

static status_t handle_rdmsr(const ExitInfo& exit_info, GuestState* guest_state) {
    switch (guest_state->rcx) {
    case X86_MSR_IA32_APIC_BASE:
        next_rip(exit_info);
        guest_state->rax = kLocalApicPhysBase;
        guest_state->rdx = 0;
        return MX_OK;
    // From Volume 3, Section 35.1, Table 35-2 (p. 35-11): For now, only
    // enable fast strings.
    case X86_MSR_IA32_MISC_ENABLE:
        next_rip(exit_info);
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
    // From Volume 3, Section 35.1, Table 35-2 (p. 35-13): For now, 0.
    case X86_MSR_IA32_PLATFORM_ID:
    // From Volume 3, Section 35.1, Table 35-2 (p. 35-5): 0 indicates no
    // microcode update is loaded.
    case X86_MSR_IA32_BIOS_SIGN_ID:
        next_rip(exit_info);
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
    uint8_t interrupt = *lvt_timer & LVT_TIMER_VECTOR_MASK;
    local_apic_signal_interrupt(local_apic_state, interrupt, false);
    return INT_NO_RESCHEDULE;
}

static status_t handle_wrmsr(const ExitInfo& exit_info, GuestState* guest_state,
                             LocalApicState* local_apic_state) {
    switch (guest_state->rcx) {
    case X86_MSR_IA32_APIC_BASE:
        if (guest_state->rax != kLocalApicPhysBase || guest_state->rdx != 0)
            return MX_ERR_INVALID_ARGS;
        next_rip(exit_info);
        return MX_OK;
    // See note in handle_rdmsr.
    case X86_MSR_IA32_MTRRCAP:
    case X86_MSR_IA32_MTRR_DEF_TYPE:
    case X86_MSR_IA32_MTRR_FIX64K_00000:
    case X86_MSR_IA32_MTRR_FIX16K_80000 ... X86_MSR_IA32_MTRR_FIX16K_A0000:
    case X86_MSR_IA32_MTRR_FIX4K_C0000 ... X86_MSR_IA32_MTRR_FIX4K_F8000:
    case X86_MSR_IA32_MTRR_PHYSBASE0 ... X86_MSR_IA32_MTRR_PHYSMASK9:
    case X86_MSR_IA32_BIOS_SIGN_ID:
        next_rip(exit_info);
        return MX_OK;
    case X86_MSR_IA32_TSC_DEADLINE: {
        uint32_t* reg = apic_reg(local_apic_state, kLocalApicLvtTimer);
        if ((*reg & LVT_TIMER_MODE_MASK) != LVT_TIMER_MODE_TSC_DEADLINE)
            return MX_ERR_INVALID_ARGS;
        next_rip(exit_info);
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

static status_t get_page(GuestPhysicalAddressSpace* gpas, vaddr_t guest_vaddr,
                         paddr_t* host_paddr) {
    size_t indices[X86_PAGING_LEVELS] = {
        VADDR_TO_PML4_INDEX(guest_vaddr),
        VADDR_TO_PDP_INDEX(guest_vaddr),
        VADDR_TO_PD_INDEX(guest_vaddr),
        VADDR_TO_PT_INDEX(guest_vaddr),
    };
    paddr_t pt_addr = vmcs_read(VmcsFieldXX::GUEST_CR3);
    paddr_t pa;
    for (size_t level = 0; level <= X86_PAGING_LEVELS; level++) {
        status_t status = gpas->GetPage(page_addr(pt_addr, level - 1, guest_vaddr), &pa);
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

static status_t fetch_data(GuestPhysicalAddressSpace* gpas, vaddr_t guest_vaddr, uint8_t* data,
                           size_t size) {
    // TODO(abdulla): Make this handle a fetch that crosses more than two pages.
    if (size > PAGE_SIZE)
        return MX_ERR_OUT_OF_RANGE;

    paddr_t pa;
    status_t status = get_page(gpas, guest_vaddr, &pa);
    if (status != MX_OK)
        return status;

    size_t page_offset = guest_vaddr & PAGE_OFFSET_MASK_4KB;
    uint8_t* page = static_cast<uint8_t*>(paddr_to_kvaddr(pa));
    size_t from_page = mxtl::min(size, PAGE_SIZE - page_offset);
    // TODO(security): This should be a volatile memcpy.
    memcpy(data, page + page_offset, from_page);

    // If the fetch is not split across pages, return.
    if (from_page == size)
        return MX_OK;

    status = get_page(gpas, guest_vaddr + size, &pa);
    if (status != MX_OK)
        return status;

    page = static_cast<uint8_t*>(paddr_to_kvaddr(pa));
    memcpy(data + from_page, page, size - from_page);
    return MX_OK;
}

#if WITH_LIB_MAGENTA
static status_t handle_mem_trap(const ExitInfo& exit_info, AutoVmcsLoad* vmcs_load,
                                vaddr_t guest_paddr, GuestPhysicalAddressSpace* gpas,
                                FifoDispatcher* ctl_fifo) {
    if (exit_info.instruction_length > X86_MAX_INST_LEN)
        return MX_ERR_INTERNAL;

    mx_guest_packet_t packet;
    memset(&packet, 0, sizeof(mx_guest_packet_t));
    packet.type = MX_GUEST_PKT_TYPE_MEM_TRAP;
    packet.mem_trap.guest_paddr = guest_paddr;
    packet.mem_trap.instruction_length = exit_info.instruction_length & UINT8_MAX;
    status_t status = fetch_data(gpas, exit_info.guest_rip, packet.mem_trap.instruction_buffer,
                                 packet.mem_trap.instruction_length);
    if (status != MX_OK)
        return status;
    status = packet_write(vmcs_load, ctl_fifo, packet);
    if (status != MX_OK)
        return status;
    status = packet_read(vmcs_load, ctl_fifo, &packet);
    if (status != MX_OK)
        return status;
    if (packet.type != MX_GUEST_PKT_TYPE_MEM_TRAP)
        return MX_ERR_INVALID_ARGS;
    if (packet.mem_trap_ret.fault) {
        issue_interrupt(X86_INT_GP_FAULT, 0, InterruptionType::HARDWARE_EXCEPTION);
    } else {
        next_rip(exit_info);
    }
    return MX_OK;
}
#endif // WITH_LIB_MAGENTA

static status_t handle_apic_access(const ExitInfo& exit_info, AutoVmcsLoad* vmcs_load,
                                   LocalApicState* local_apic_state,
                                   GuestPhysicalAddressSpace* gpas, FifoDispatcher* ctl_fifo) {
#if WITH_LIB_MAGENTA
    ApicAccessInfo apic_access_info(exit_info.exit_qualification);
    switch (apic_access_info.access_type) {
    default:
        return MX_ERR_NOT_SUPPORTED;
    case ApicAccessType::LINEAR_ACCESS_WRITE:
        if (apic_access_info.offset == kLocalApicEoi) {
            // When we observe an EOI, we issue any pending interrupts. This is
            // not architecture-accurate, but works for the virtual machine.
            local_apic_maybe_interrupt(local_apic_state);
            next_rip(exit_info);
            return MX_OK;
        }
        /* fallthrough */
    case ApicAccessType::LINEAR_ACCESS_READ:
        vaddr_t guest_paddr = APIC_PHYS_BASE + apic_access_info.offset;
        return handle_mem_trap(exit_info, vmcs_load, guest_paddr, gpas, ctl_fifo);
    }
#else // WITH_LIB_MAGENTA
    return MX_ERR_NOT_SUPPORTED;
#endif // WITH_LIB_MAGENTA
}

static status_t handle_ept_violation(const ExitInfo& exit_info, AutoVmcsLoad* vmcs_load,
                                     GuestPhysicalAddressSpace* gpas, FifoDispatcher* ctl_fifo) {
#if WITH_LIB_MAGENTA
    vaddr_t guest_paddr = exit_info.guest_physical_address;
    return handle_mem_trap(exit_info, vmcs_load, guest_paddr, gpas, ctl_fifo);
#else // WITH_LIB_MAGENTA
    return MX_ERR_NOT_SUPPORTED;
#endif // WITH_LIB_MAGENTA
}

static status_t handle_xsetbv(const ExitInfo& exit_info, GuestState* guest_state) {
    uint64_t guest_cr4 = vmcs_read(VmcsFieldXX::GUEST_CR4);
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
    next_rip(exit_info);
    return MX_OK;
}

status_t vmexit_handler(AutoVmcsLoad* vmcs_load, GuestState* guest_state,
                        LocalApicState* local_apic_state, GuestPhysicalAddressSpace* gpas,
                        FifoDispatcher* ctl_fifo) {
    ExitInfo exit_info;

    switch (exit_info.exit_reason) {
    case ExitReason::EXTERNAL_INTERRUPT:
        return handle_external_interrupt(vmcs_load, local_apic_state);
    case ExitReason::INTERRUPT_WINDOW:
        LTRACEF("handling interrupt window\n\n");
        return handle_interrupt_window(local_apic_state);
    case ExitReason::CPUID:
        LTRACEF("handling CPUID instruction\n\n");
        return handle_cpuid(exit_info, guest_state);
    case ExitReason::HLT:
        LTRACEF("handling HLT instruction\n\n");
        return handle_hlt(exit_info, vmcs_load, local_apic_state);
    case ExitReason::VMCALL:
        LTRACEF("handling VMCALL instruction\n\n");
        return MX_ERR_STOP;
    case ExitReason::IO_INSTRUCTION:
        return handle_io_instruction(exit_info, vmcs_load, guest_state, ctl_fifo);
    case ExitReason::RDMSR:
        LTRACEF("handling RDMSR instruction %#" PRIx64 "\n\n", guest_state->rcx);
        return handle_rdmsr(exit_info, guest_state);
    case ExitReason::WRMSR:
        LTRACEF("handling WRMSR instruction %#" PRIx64 "\n\n", guest_state->rcx);
        return handle_wrmsr(exit_info, guest_state, local_apic_state);
    case ExitReason::ENTRY_FAILURE_GUEST_STATE:
    case ExitReason::ENTRY_FAILURE_MSR_LOADING:
        LTRACEF("handling VM entry failure\n\n");
        return MX_ERR_BAD_STATE;
    case ExitReason::APIC_ACCESS:
        LTRACEF("handling APIC access\n\n");
        return handle_apic_access(exit_info, vmcs_load, local_apic_state, gpas, ctl_fifo);
    case ExitReason::EPT_VIOLATION:
        LTRACEF("handling EPT violation\n\n");
        return handle_ept_violation(exit_info, vmcs_load, gpas, ctl_fifo);
    case ExitReason::XSETBV:
        LTRACEF("handling XSETBV instruction\n\n");
        return handle_xsetbv(exit_info, guest_state);
    default:
        LTRACEF("unhandled VM exit %u\n\n", static_cast<uint32_t>(exit_info.exit_reason));
        return MX_ERR_NOT_SUPPORTED;
    }
}
