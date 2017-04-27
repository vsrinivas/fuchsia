// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <string.h>

#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <arch/x86/mmu.h>
#include <hypervisor/guest_physical_address_space.h>
#include <mxtl/algorithm.h>

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
static const uint8_t kIoApicVersion = 0x11;
static const uint32_t kFirstRedirectOffset = 0x10;
static const uint32_t kLastRedirectOffset = kFirstRedirectOffset + kIoApicRedirectOffsets - 1;

static const uint32_t kMaxInstructionLength = 15;
static const uint8_t kRexRMask = 1u << 2;
static const uint8_t kRexWMask = 1u << 3;

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
            // Disable the TSC deadline bit.
            guest_state->rcx &= ~(1u << X86_FEATURE_TSC_DEADLINE.bit);
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
        return (mod_rm & 0b11000111) == 0b00000101 ? 4 : 0;
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
        inst->val = 0;
        inst->reg = select_register(guest_state, register_id(mod_rm, rex_r));
        return inst->reg == nullptr ? ERR_NOT_SUPPORTED : NO_ERROR;
    // Move r/m to r.
    case 0x8b:
        if (inst_len != disp_size + 2u)
            return ERR_OUT_OF_RANGE;
        inst->read = true;
        inst->rex = rex_w;
        inst->val = 0;
        inst->reg = select_register(guest_state, register_id(mod_rm, rex_r));
        return inst->reg == nullptr ? ERR_NOT_SUPPORTED : NO_ERROR;
    // Move imm to r/m.
    case 0xc7: {
        const uint8_t imm_size = 4;
        if (inst_len != disp_size + imm_size + 2u)
            return ERR_OUT_OF_RANGE;
        if ((mod_rm & 0b00111000) != 0)
            return ERR_INVALID_ARGS;
        inst->read = false;
        inst->rex = false;
        inst->val = 0;
        inst->reg = nullptr;
        memcpy(&inst->val, inst_buf + disp_size + 2, imm_size);
        return NO_ERROR;
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

template<typename T>
T get_value(const Instruction& inst) {
    return static_cast<T>(inst.reg != nullptr ? *inst.reg : inst.val);
}

static status_t handle_ept_violation(const ExitInfo& exit_info, GuestState* guest_state,
                                     IoApicState* io_apic_state, GuestPhysicalAddressSpace* gpas) {
    if (exit_info.guest_physical_address < kIoApicPhysBase ||
        exit_info.guest_physical_address >= kIoApicPhysBase + PAGE_SIZE)
        return ERR_OUT_OF_RANGE;

    uint8_t inst_buf[kMaxInstructionLength];
    uint32_t inst_len = exit_info.instruction_length;
    status_t status = fetch_data(gpas, exit_info.guest_rip, inst_buf, inst_len);
    if (status != NO_ERROR)
        return status;

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
        next_rip(exit_info);
        io_apic_state->select = get_value<uint32_t>(inst);
        return io_apic_state->select > UINT8_MAX ? ERR_INVALID_ARGS : NO_ERROR;
    case IO_APIC_IOWIN:
        switch (io_apic_state->select) {
        case IO_APIC_REG_ID:
            next_rip(exit_info);
            if (inst.read)
                *inst.reg = io_apic_state->id;
            else
                io_apic_state->id = get_value<uint32_t>(inst);
            return NO_ERROR;
        case IO_APIC_REG_VER:
            if (!inst.read || inst.reg == nullptr)
                return ERR_NOT_SUPPORTED;
            next_rip(exit_info);
            // There are two redirect offsets per redirection entry. We return
            // the maximum redirection entry index.
            //
            // From Intel 82093AA, Section 3.2.2.
            *inst.reg = (kIoApicRedirectOffsets / 2 - 1) << 16 | kIoApicVersion;
            return NO_ERROR;
        case kFirstRedirectOffset ... kLastRedirectOffset: {
            next_rip(exit_info);
            uint32_t i = io_apic_state->select - kFirstRedirectOffset;
            if (inst.read)
                *inst.reg = io_apic_state->redirect[i];
            else
                io_apic_state->redirect[i] = get_value<uint32_t>(inst);
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
                        IoApicState* io_apic_state, GuestPhysicalAddressSpace* gpas,
                        FifoDispatcher* serial_fifo) {
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
    case ExitReason::EPT_VIOLATION:
        dprintf(SPEW, "handling EPT violation\n\n");
        return handle_ept_violation(exit_info, guest_state, io_apic_state, gpas);
    case ExitReason::XSETBV:
        dprintf(SPEW, "handling XSETBV instruction\n\n");
        return handle_xsetbv(exit_info, guest_state);
    default:
        dprintf(SPEW, "unhandled VM exit %u\n\n", static_cast<uint32_t>(exit_info.exit_reason));
        return ERR_NOT_SUPPORTED;
    }
}
