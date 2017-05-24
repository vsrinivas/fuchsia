// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <platform.h>
#include <string.h>

#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <arch/x86/interrupts.h>
#include <arch/x86/mmu.h>
#include <hypervisor/guest_physical_address_space.h>
#include <kernel/sched.h>
#include <kernel/timer.h>
#include <mxtl/algorithm.h>
#include <platform/pc/timer.h>

#include "hypervisor_priv.h"
#include "vmexit_priv.h"

#if WITH_LIB_MAGENTA
#include <magenta/fifo_dispatcher.h>
#include <magenta/syscalls/hypervisor.h>

static const uint16_t kUartReceiveIoPort = 0x3f8;
static const uint16_t kUartStatusIoPort = 0x3fd;
static const uint64_t kUartStatusIdle = 1u << 6;
#endif // WITH_LIB_MAGENTA

static const uint64_t kIa32ApicBase =
    APIC_PHYS_BASE | IA32_APIC_BASE_BSP | IA32_APIC_BASE_XAPIC_ENABLE;
static const uint8_t kIoApicVersion = 0x11;
static const uint32_t kFirstRedirectOffset = 0x10;
static const uint32_t kLastRedirectOffset = kFirstRedirectOffset + kIoApicRedirectOffsets - 1;

static const uint8_t kApicAccessRead = 0;

static const uint32_t kMaxInstructionLength = 15;
static const uint8_t kRexRMask = 1u << 2;
static const uint8_t kRexWMask = 1u << 3;
static const uint8_t kModRMRegMask = 0b00111000;

static const uint32_t kInterruptInfoDeliverErrorCode = 1u << 11;
static const uint32_t kInterruptInfoValid = 1u << 31;
static const uint64_t kInvalidErrorCode = UINT64_MAX;

ExitInfo::ExitInfo() {
    exit_reason = static_cast<ExitReason>(vmcs_read(VmcsField32::EXIT_REASON));
    exit_qualification = vmcs_read(VmcsFieldXX::EXIT_QUALIFICATION);
    instruction_length = vmcs_read(VmcsField32::EXIT_INSTRUCTION_LENGTH);
    guest_physical_address = vmcs_read(VmcsField64::GUEST_PHYSICAL_ADDRESS);
    guest_rip = vmcs_read(VmcsFieldXX::GUEST_RIP);

    if (exit_reason == ExitReason::EXTERNAL_INTERRUPT ||
        exit_reason == ExitReason::IO_INSTRUCTION)
        return;

    dprintf(SPEW, "exit reason: %#" PRIx32 "\n", static_cast<uint32_t>(exit_reason));
    dprintf(SPEW, "exit qualification: %#" PRIx64 "\n", exit_qualification);
    dprintf(SPEW, "instruction length: %#" PRIx32 "\n", instruction_length);
    dprintf(SPEW, "guest physical address: %#" PRIx64 "\n", guest_physical_address);
    dprintf(SPEW, "guest linear address: %#" PRIx64 "\n",
        vmcs_read(VmcsFieldXX::GUEST_LINEAR_ADDRESS));
    dprintf(SPEW, "guest activity state: %#" PRIx32 "\n",
        vmcs_read(VmcsField32::GUEST_ACTIVITY_STATE));
    dprintf(SPEW, "guest interruptibility state: %#" PRIx32 "\n",
        vmcs_read(VmcsField32::GUEST_INTERRUPTIBILITY_STATE));
    dprintf(SPEW, "guest rip: %#" PRIx64 "\n", guest_rip);
}

IoInfo::IoInfo(uint64_t qualification) {
    access_size = static_cast<uint8_t>(BITS(qualification, 2, 0) + 1);
    input = BIT_SHIFT(qualification, 3);
    string = BIT_SHIFT(qualification, 4);
    repeat = BIT_SHIFT(qualification, 5);
    port = static_cast<uint16_t>(BITS_SHIFT(qualification, 31, 16));
}

ApicAccessInfo::ApicAccessInfo(uint64_t qualification) {
    reg = static_cast<ApicRegister>(BITS(qualification, 11, 0));
    type = static_cast<uint8_t>(BITS_SHIFT(qualification, 15, 12));
}

static void next_rip(const ExitInfo& exit_info) {
    vmcs_write(VmcsFieldXX::GUEST_RIP, exit_info.guest_rip + exit_info.instruction_length);
}

static void set_interrupt(uint32_t interrupt, uint64_t error_code, InterruptionType type) {
    uint32_t interrupt_info = kInterruptInfoValid | static_cast<uint32_t>(type) << 8 | interrupt;
    if (error_code != kInvalidErrorCode) {
        interrupt_info |= kInterruptInfoDeliverErrorCode;
        vmcs_write(VmcsField32::ENTRY_EXCEPTION_ERROR_CODE, error_code & UINT32_MAX);
    }
    vmcs_write(VmcsField32::ENTRY_INTERRUPTION_INFORMATION, interrupt_info);
}

static void set_local_apic_interrupt(LocalApicState* local_apic_state) {
    if (local_apic_state->active_interrupt == kInvalidInterrupt)
        return;
    set_interrupt(local_apic_state->active_interrupt, kInvalidErrorCode,
                  InterruptionType::EXTERNAL_INTERRUPT);
    local_apic_state->active_interrupt = kInvalidInterrupt;
}

void interrupt_window_exiting(bool enable) {
    uint32_t controls = vmcs_read(VmcsField32::PROCBASED_CTLS);
    if (enable) {
        controls |= PROCBASED_CTLS_INT_WINDOW_EXITING;
    }
    else {
        controls &= ~PROCBASED_CTLS_INT_WINDOW_EXITING;
    }
    vmcs_write(VmcsField32::PROCBASED_CTLS, controls);
}

static status_t handle_external_interrupt(const ExitInfo& exit_info, AutoVmcsLoad* vmcs_load,
                                          LocalApicState* local_apic_state) {
    vmcs_load->reload();
    if (vmcs_read(VmcsFieldXX::GUEST_RFLAGS) & X86_FLAGS_IF) {
        // If interrupts are enabled, we inject any active interrupts.
        set_local_apic_interrupt(local_apic_state);
    } else if (local_apic_state->active_interrupt != kInvalidInterrupt) {
        // If interrupts are disabled, we set VM exit on interrupt enable.
        interrupt_window_exiting(true);
    }
    return NO_ERROR;
}

static status_t handle_interrupt_window(const ExitInfo& exit_info,
                                        LocalApicState* local_apic_state) {
    interrupt_window_exiting(false);
    set_local_apic_interrupt(local_apic_state);
    return NO_ERROR;
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
            // Disable the VMX bit.
            guest_state->rcx &= ~(1u << X86_FEATURE_VMX.bit);
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

static status_t handle_hlt(const ExitInfo& exit_info, LocalApicState* local_apic_state) {
    // TODO(abdulla): Use an interruptible sleep here, so that we can:
    // a) Continue to deliver interrupts to the guest.
    // b) Kill the hypervisor while a guest is halted.
    event_wait(&local_apic_state->event);
    set_local_apic_interrupt(local_apic_state);
    next_rip(exit_info);
    return NO_ERROR;
}

#if WITH_LIB_MAGENTA
class FifoStateObserver : public StateObserver {
public:
    FifoStateObserver(mx_signals_t watched_signals)
        : watched_signals_(watched_signals) {
        event_init(&event_, false, 0);
    }
    status_t Wait(StateTracker* state_tracker) {
        state_tracker->AddObserver(this, nullptr);
        status_t status = event_wait(&event_);
        state_tracker->RemoveObserver(this);
        return status;
    }
private:
    mx_signals_t watched_signals_;
    event_t event_;
    virtual bool OnInitialize(mx_signals_t initial_state, const CountInfo* cinfo) {
        return false;
    }
    virtual bool OnStateChange(mx_signals_t new_state) {
        if (new_state & watched_signals_)
            event_signal(&event_, false);
        return false;
    }
    virtual bool OnCancel(Handle* handle) {
        return false;
    }
};

static status_t packet_wait(StateTracker* state_tracker, mx_signals_t signals) {
    if (state_tracker->GetSignalsState() & signals)
        return NO_ERROR;
    // TODO(abdulla): Add stats to keep track of waits.
    FifoStateObserver state_observer(signals | MX_FIFO_PEER_CLOSED);
    status_t status = state_observer.Wait(state_tracker);
    if (status != NO_ERROR)
        return status;
    return state_tracker->GetSignalsState() & MX_FIFO_PEER_CLOSED ? ERR_PEER_CLOSED : NO_ERROR;
}

static status_t packet_write(FifoDispatcher* ctl_fifo, const mx_guest_packet_t& packet) {
    status_t status = packet_wait(ctl_fifo->get_state_tracker(), MX_FIFO_WRITABLE);
    if (status != NO_ERROR)
        return status;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(&packet);
    uint32_t actual;
    status = ctl_fifo->Write(data, sizeof(mx_guest_packet_t), &actual);
    if (status != NO_ERROR)
        return status;
    return actual != 1 ? ERR_IO_DATA_INTEGRITY : NO_ERROR;
}

static status_t packet_read(FifoDispatcher* ctl_fifo, mx_guest_packet_t* packet) {
    status_t status = packet_wait(ctl_fifo->get_state_tracker(), MX_FIFO_READABLE);
    if (status != NO_ERROR)
        return status;
    uint8_t* data = reinterpret_cast<uint8_t*>(packet);
    uint32_t actual;
    status = ctl_fifo->Read(data, sizeof(mx_guest_packet_t), &actual);
    if (status != NO_ERROR)
        return status;
    return actual != 1 ? ERR_IO_DATA_INTEGRITY : NO_ERROR;
}
#endif // WITH_LIB_MAGENTA

static status_t handle_io_instruction(const ExitInfo& exit_info, GuestState* guest_state,
                                      FifoDispatcher* ctl_fifo) {
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
    mx_guest_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = MX_GUEST_PKT_TYPE_IO_PORT;
    packet.io_port.access_size = io_info.access_size;
    memcpy(packet.io_port.data, &guest_state->rax, io_info.access_size);
    return packet_write(ctl_fifo, packet);
    return NO_ERROR;
#else // WITH_LIB_MAGENTA
    return ERR_NOT_SUPPORTED;
#endif // WITH_LIB_MAGENTA
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

static uint32_t* apic_reg(LocalApicState* local_apic_state, ApicRegister reg) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(local_apic_state->apic_addr);
    return reinterpret_cast<uint32_t*>(addr + static_cast<uint16_t>(reg));
}

static handler_return deadline_callback(timer_t* timer, lk_time_t now, void* arg) {
    LocalApicState* local_apic_state = static_cast<LocalApicState*>(arg);
    DEBUG_ASSERT(local_apic_state->active_interrupt == kInvalidInterrupt);

    uint32_t* lvt_timer = apic_reg(local_apic_state, ApicRegister::LVT_TIMER);
    local_apic_state->active_interrupt = *lvt_timer & LVT_TIMER_VECTOR_MASK;
    local_apic_state->tsc_deadline = 0;
    event_signal(&local_apic_state->event, false);
    return INT_NO_RESCHEDULE;
}

static status_t handle_wrmsr(const ExitInfo& exit_info, GuestState* guest_state,
                             LocalApicState* local_apic_state) {
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
    case X86_MSR_IA32_TSC_DEADLINE: {
        uint32_t* reg = apic_reg(local_apic_state, ApicRegister::LVT_TIMER);
        if ((*reg & LVT_TIMER_MODE_MASK) != LVT_TIMER_MODE_TSC_DEADLINE)
            return ERR_INVALID_ARGS;
        next_rip(exit_info);
        timer_cancel(&local_apic_state->timer);
        local_apic_state->active_interrupt = kInvalidInterrupt;
        local_apic_state->tsc_deadline = guest_state->rdx << 32 | (guest_state->rax & UINT32_MAX);
        if (local_apic_state->tsc_deadline > 0) {
            lk_time_t deadline = ticks_to_nanos(local_apic_state->tsc_deadline);
            timer_set_oneshot(&local_apic_state->timer, deadline, deadline_callback, local_apic_state);
        }
        return NO_ERROR;
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
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
    for (size_t i = 0; i <= X86_PAGING_LEVELS; i++) {
        status_t status = gpas->GetPage(pt_addr & X86_PG_FRAME, &pa);
        if (status != NO_ERROR)
            return status;
        if (i == X86_PAGING_LEVELS)
            break;
        pt_entry_t* pt = static_cast<pt_entry_t*>(paddr_to_kvaddr(pa));
        pt_addr = pt[indices[i]];
        if (!IS_PAGE_PRESENT(pt_addr))
            return ERR_NOT_FOUND;
        if (IS_LARGE_PAGE(pt_addr))
            break;
    }
    *host_paddr = pa;
    return NO_ERROR;
}

static status_t fetch_data(GuestPhysicalAddressSpace* gpas, vaddr_t guest_vaddr, uint8_t* data,
                           size_t size) {
    // TODO(abdulla): Make this handle a fetch that crosses more than two pages.
    if (size > PAGE_SIZE)
        return ERR_OUT_OF_RANGE;

    paddr_t pa;
    status_t status = get_page(gpas, guest_vaddr, &pa);
    if (status != NO_ERROR)
        return status;

    size_t page_offset = guest_vaddr & PAGE_OFFSET_MASK_4KB;
    uint8_t* page = static_cast<uint8_t*>(paddr_to_kvaddr(pa));
    size_t from_page = mxtl::min(size, PAGE_SIZE - page_offset);
    // TODO(security): This should be a volatile memcpy.
    memcpy(data, page + page_offset, from_page);

    // If the fetch is not split across pages, return.
    if (from_page == size)
        return NO_ERROR;

    status = get_page(gpas, guest_vaddr + size, &pa);
    if (status != NO_ERROR)
        return status;

    page = static_cast<uint8_t*>(paddr_to_kvaddr(pa));
    memcpy(data + from_page, page, size - from_page);
    return NO_ERROR;
}

static bool is_rex_prefix(uint8_t prefix) {
    return (prefix >> 4) == 0b0100;
}

static bool has_sib_byte(uint8_t mod_rm) {
    return (mod_rm >> 6) != 0b11 && (mod_rm & 0b111) == 0b100;
}

static uint8_t displacement_size(uint8_t mod_rm) {
    switch (mod_rm >> 6) {
    case 0b01:
        return 1;
    case 0b10:
        return 4;
    default:
        return (mod_rm & ~kModRMRegMask) == 0b00000101 ? 4 : 0;
    }
}

static uint8_t register_id(uint8_t mod_rm, bool rex_r) {
    return static_cast<uint8_t>(((mod_rm >> 3) & 0b111) + (rex_r ? 0b1000 : 0));
}

static uint64_t* select_register(GuestState* guest_state, uint8_t register_id) {
    // From Intel Volume 2, Section 2.1.
    switch (register_id) {
    // From Intel Volume 2, Section 2.1.5.
    case 0:
        return &guest_state->rax;
    case 1:
        return &guest_state->rcx;
    case 2:
        return &guest_state->rdx;
    case 3:
        return &guest_state->rbx;
    case 4:
        // RSP is specially handled by the VMCS.
    default:
        return nullptr;
    case 5:
        return &guest_state->rbp;
    case 6:
        return &guest_state->rsi;
    case 7:
        return &guest_state->rdi;
    case 8:
        return &guest_state->r8;
    case 9:
        return &guest_state->r9;
    case 10:
        return &guest_state->r10;
    case 11:
        return &guest_state->r11;
    case 12:
        return &guest_state->r12;
    case 13:
        return &guest_state->r13;
    case 14:
        return &guest_state->r14;
    case 15:
        return &guest_state->r15;
    }
}

status_t decode_instruction(const uint8_t* inst_buf, uint32_t inst_len, GuestState* guest_state,
                            Instruction* inst) {
    if (inst_len == 0)
        return ERR_BAD_STATE;
    if (inst_len > kMaxInstructionLength)
        return ERR_OUT_OF_RANGE;

    // Parse REX prefix.
    //
    // From Intel Volume 2, Appendix 2.2.1.
    //
    // TODO(abdulla): Handle more prefixes.
    bool rex_r = false;
    bool rex_w = false;
    if (is_rex_prefix(inst_buf[0])) {
        rex_r = inst_buf[0] & kRexRMask;
        rex_w = inst_buf[0] & kRexWMask;
        inst_buf++;
        inst_len--;
    }

    if (inst_len == 0)
        return ERR_NOT_SUPPORTED;
    if (inst_len < 2)
        return ERR_OUT_OF_RANGE;

    const uint8_t mod_rm = inst_buf[1];
    if (has_sib_byte(mod_rm))
        return ERR_NOT_SUPPORTED;

    const uint8_t disp_size = displacement_size(mod_rm);
    switch (inst_buf[0]) {
    // Move r to r/m.
    case 0x89:
        if (inst_len != disp_size + 2u)
            return ERR_OUT_OF_RANGE;
        inst->read = false;
        inst->rex = rex_w;
        inst->imm = 0;
        inst->reg = select_register(guest_state, register_id(mod_rm, rex_r));
        return inst->reg == nullptr ? ERR_NOT_SUPPORTED : NO_ERROR;
    // Move r/m to r.
    case 0x8b:
        if (inst_len != disp_size + 2u)
            return ERR_OUT_OF_RANGE;
        inst->read = true;
        inst->rex = rex_w;
        inst->imm = 0;
        inst->reg = select_register(guest_state, register_id(mod_rm, rex_r));
        return inst->reg == nullptr ? ERR_NOT_SUPPORTED : NO_ERROR;
    // Move imm to r/m.
    case 0xc7: {
        const uint8_t imm_size = 4;
        if (inst_len != disp_size + imm_size + 2u)
            return ERR_OUT_OF_RANGE;
        if ((mod_rm & kModRMRegMask) != 0)
            return ERR_INVALID_ARGS;
        inst->read = false;
        inst->rex = false;
        inst->imm = 0;
        inst->reg = nullptr;
        memcpy(&inst->imm, inst_buf + disp_size + 2, imm_size);
        return NO_ERROR;
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

template<typename T>
T get_value(const Instruction& inst) {
    return static_cast<T>(inst.reg != nullptr ? *inst.reg : inst.imm);
}

template<typename T>
void apply_inst(const Instruction& inst, T* value) {
    if (inst.read)
        *inst.reg = *value;
    else
        *value = get_value<T>(inst);
}

static status_t handle_apic_access(const ExitInfo& exit_info, GuestState* guest_state,
                                   LocalApicState* local_apic_state,
                                   GuestPhysicalAddressSpace* gpas) {
    uint8_t inst_buf[kMaxInstructionLength];
    uint32_t inst_len = exit_info.instruction_length;
    status_t status = fetch_data(gpas, exit_info.guest_rip, inst_buf, inst_len);
    if (status != NO_ERROR)
        return status;

    Instruction inst;
    status = decode_instruction(inst_buf, inst_len, guest_state, &inst);
    if (status != NO_ERROR)
        return status;

    ApicAccessInfo apic_access_info(exit_info.exit_qualification);
    if (inst.read && apic_access_info.type != kApicAccessRead)
        return ERR_BAD_STATE;

    switch (apic_access_info.reg) {
    case ApicRegister::LOCAL_APIC_ID:
        if (!inst.read)
            return ERR_NOT_SUPPORTED;
        *inst.reg = 0;
        next_rip(exit_info);
        return NO_ERROR;
    case ApicRegister::EOI:
        // TODO(abdulla): Correctly handle EOI.
        if (inst.read)
            return ERR_INVALID_ARGS;
        next_rip(exit_info);
        return NO_ERROR;
    case ApicRegister::SVR:
    case ApicRegister::ESR:
    case ApicRegister::LVT_TIMER:
    case ApicRegister::LVT_ERROR: {
        next_rip(exit_info);
        // From Intel Volume 3, Section 10.5.3: Before attempt to read from the
        // ESR, software should first write to it.
        //
        // Therefore, we ignore writes to the ESR.
        if (!inst.read && apic_access_info.reg == ApicRegister::ESR)
            return NO_ERROR;
        uint32_t* reg = apic_reg(local_apic_state, apic_access_info.reg);
        apply_inst(inst, reg);
        return NO_ERROR;
    }
    case ApicRegister::INITIAL_COUNT:
        uint32_t count = get_value<uint32_t>(inst);
        if (inst.read || count > 0)
            return ERR_NOT_SUPPORTED;
        next_rip(exit_info);
        return NO_ERROR;
    }
    return ERR_NOT_SUPPORTED;
}

static status_t handle_ept_violation(const ExitInfo& exit_info, GuestState* guest_state,
                                     IoApicState* io_apic_state, GuestPhysicalAddressSpace* gpas,
                                     FifoDispatcher* ctl_fifo) {
    uint8_t inst_buf[kMaxInstructionLength];
    uint32_t inst_len = exit_info.instruction_length;
    status_t status = fetch_data(gpas, exit_info.guest_rip, inst_buf, inst_len);
    if (status != NO_ERROR)
        return status;

    if (exit_info.guest_physical_address < kIoApicPhysBase ||
        exit_info.guest_physical_address >= kIoApicPhysBase + PAGE_SIZE) {
#if WITH_LIB_MAGENTA
        mx_guest_packet_t packet;
        memset(&packet, 0, sizeof(mx_guest_packet_t));
        packet.type = MX_GUEST_PKT_TYPE_MEM_TRAP;
        packet.mem_trap.instruction_length = exit_info.instruction_length & UINT8_MAX;
        memcpy(packet.mem_trap.instruction, inst_buf, inst_len);
        packet.mem_trap.paddr = exit_info.guest_physical_address;
        status_t status = packet_write(ctl_fifo, packet);
        if (status != NO_ERROR)
            return status;
        status = packet_read(ctl_fifo, &packet);
        if (status != NO_ERROR)
            return status;
        if (packet.type != MX_GUEST_PKT_TYPE_MEM_TRAP_ACTION)
            return ERR_INVALID_ARGS;
        if (packet.mem_trap_action.fault) {
            // Inject a GP fault if there was an EPT violation outside of the IO APIC page.
            set_interrupt(X86_INT_GP_FAULT, 0, InterruptionType::HARDWARE_EXCEPTION);
        } else {
            next_rip(exit_info);
        }
        return NO_ERROR;
#else // WITH_LIB_MAGENTA
        return ERR_NOT_SUPPORTED;
#endif // WITH_LIB_MAGENTA
    }

    Instruction inst;
    status = decode_instruction(inst_buf, inst_len, guest_state, &inst);
    if (status != NO_ERROR)
        return status;
    if (inst.rex)
        return ERR_NOT_SUPPORTED;

    uint64_t io_apic_reg = exit_info.guest_physical_address - kIoApicPhysBase;
    switch (io_apic_reg) {
    case IO_APIC_IOREGSEL:
        if (inst.read)
            return ERR_NOT_SUPPORTED;
        io_apic_state->select = get_value<uint32_t>(inst);
        next_rip(exit_info);
        return io_apic_state->select > UINT8_MAX ? ERR_INVALID_ARGS : NO_ERROR;
    case IO_APIC_IOWIN:
        switch (io_apic_state->select) {
        case IO_APIC_REG_ID:
            apply_inst(inst, &io_apic_state->id);
            next_rip(exit_info);
            return NO_ERROR;
        case IO_APIC_REG_VER:
            if (!inst.read || inst.reg == nullptr)
                return ERR_NOT_SUPPORTED;
            // There are two redirect offsets per redirection entry. We return
            // the maximum redirection entry index.
            //
            // From Intel 82093AA, Section 3.2.2.
            *inst.reg = (kIoApicRedirectOffsets / 2 - 1) << 16 | kIoApicVersion;
            next_rip(exit_info);
            return NO_ERROR;
        case kFirstRedirectOffset ... kLastRedirectOffset: {
            uint32_t i = io_apic_state->select - kFirstRedirectOffset;
            apply_inst(inst, io_apic_state->redirect + i);
            next_rip(exit_info);
            return NO_ERROR;
        }}
    }
    return ERR_NOT_SUPPORTED;
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
    uint64_t xcr0 = (guest_state->rdx << 32) | (guest_state->rax & UINT32_MAX);
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

status_t vmexit_handler(AutoVmcsLoad* vmcs_load, GuestState* guest_state,
                        LocalApicState* local_apic_state, IoApicState* io_apic_state,
                        GuestPhysicalAddressSpace* gpas, FifoDispatcher* ctl_fifo) {
    ExitInfo exit_info;

    switch (exit_info.exit_reason) {
    case ExitReason::EXTERNAL_INTERRUPT:
        return handle_external_interrupt(exit_info, vmcs_load, local_apic_state);
    case ExitReason::INTERRUPT_WINDOW:
        dprintf(SPEW, "handling interrupt window\n\n");
        return handle_interrupt_window(exit_info, local_apic_state);
    case ExitReason::CPUID:
        dprintf(SPEW, "handling CPUID instruction\n\n");
        return handle_cpuid(exit_info, guest_state);
    case ExitReason::HLT:
        dprintf(SPEW, "handling HLT instruction\n\n");
        return handle_hlt(exit_info, local_apic_state);
    case ExitReason::VMCALL:
        dprintf(SPEW, "handling VMCALL instruction\n\n");
        return ERR_STOP;
    case ExitReason::IO_INSTRUCTION:
        return handle_io_instruction(exit_info, guest_state, ctl_fifo);
    case ExitReason::RDMSR:
        dprintf(SPEW, "handling RDMSR instruction\n\n");
        return handle_rdmsr(exit_info, guest_state);
    case ExitReason::WRMSR:
        dprintf(SPEW, "handling WRMSR instruction\n\n");
        return handle_wrmsr(exit_info, guest_state, local_apic_state);
    case ExitReason::ENTRY_FAILURE_GUEST_STATE:
    case ExitReason::ENTRY_FAILURE_MSR_LOADING:
        dprintf(SPEW, "handling VM entry failure\n\n");
        return ERR_BAD_STATE;
    case ExitReason::APIC_ACCESS:
        dprintf(SPEW, "handling APIC access\n\n");
        return handle_apic_access(exit_info, guest_state, local_apic_state, gpas);
    case ExitReason::EPT_VIOLATION:
        dprintf(SPEW, "handling EPT violation\n\n");
        return handle_ept_violation(exit_info, guest_state, io_apic_state, gpas, ctl_fifo);
    case ExitReason::XSETBV:
        dprintf(SPEW, "handling XSETBV instruction\n\n");
        return handle_xsetbv(exit_info, guest_state);
    default:
        dprintf(SPEW, "unhandled VM exit %u\n\n", static_cast<uint32_t>(exit_info.exit_reason));
        return ERR_NOT_SUPPORTED;
    }
}
