// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>

#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <hypervisor/guest_physical_address_space.h>
#include <kernel/mp.h>
#include <vm/fault.h>
#include <vm/pmm.h>
#include <vm/vm_object.h>
#include <magenta/syscalls/hypervisor.h>
#include <fbl/auto_call.h>

#include "vcpu_priv.h"
#include "vmexit_priv.h"
#include "vmx_cpu_state_priv.h"

extern uint8_t _gdt[];

static const uint kPfFlags = VMM_PF_FLAG_WRITE | VMM_PF_FLAG_SW_FAULT;

static const uint32_t kInterruptInfoValid = 1u << 31;
static const uint32_t kInterruptInfoDeliverErrorCode = 1u << 11;
static const uint32_t kInterruptTypeHardwareException = 3u << 8;

static mx_status_t vmptrld(paddr_t pa) {
    uint8_t err;

    __asm__ volatile(
        "vmptrld %[pa];" VMX_ERR_CHECK(err)
        : [err] "=r"(err)
        : [pa] "m"(pa)
        : "cc", "memory");

    return err ? MX_ERR_INTERNAL : MX_OK;
}

static mx_status_t vmclear(paddr_t pa) {
    uint8_t err;

    __asm__ volatile(
        "vmclear %[pa];" VMX_ERR_CHECK(err)
        : [err] "=r"(err)
        : [pa] "m"(pa)
        : "cc", "memory");

    return err ? MX_ERR_INTERNAL : MX_OK;
}

static uint64_t vmread(uint64_t field) {
    uint8_t err;
    uint64_t val;

    __asm__ volatile(
        "vmread %[field], %[val];" VMX_ERR_CHECK(err)
        : [err] "=r"(err), [val] "=m"(val)
        : [field] "r"(field)
        : "cc");

    DEBUG_ASSERT(err == MX_OK);
    return val;
}

static void vmwrite(uint64_t field, uint64_t val) {
    uint8_t err;

    __asm__ volatile(
        "vmwrite %[val], %[field];" VMX_ERR_CHECK(err)
        : [err] "=r"(err)
        : [val] "r"(val), [field] "r"(field)
        : "cc");

    DEBUG_ASSERT(err == MX_OK);
}

AutoVmcs::AutoVmcs(const paddr_t vmcs_address)
    : vmcs_address_(vmcs_address) {
    DEBUG_ASSERT(!arch_ints_disabled());
    arch_disable_ints();
    __UNUSED mx_status_t status = vmptrld(vmcs_address_);
    DEBUG_ASSERT(status == MX_OK);
}

AutoVmcs::~AutoVmcs() {
    DEBUG_ASSERT(arch_ints_disabled());
    arch_enable_ints();
}

void AutoVmcs::Reload() {
    DEBUG_ASSERT(arch_ints_disabled());
    __UNUSED mx_status_t status = vmptrld(vmcs_address_);
    DEBUG_ASSERT(status == MX_OK);
}

void AutoVmcs::InterruptibleReload() {
    DEBUG_ASSERT(arch_ints_disabled());
    // When we VM exit due to an external interrupt, we want to handle that
    // interrupt. To do that, we temporarily re-enable interrupts. However,
    // we must then reload the VMCS, in case it was changed in the interim.
    arch_enable_ints();
    arch_disable_ints();
    Reload();
}

void AutoVmcs::InterruptWindowExiting(bool enable) {
    uint32_t controls = Read(VmcsField32::PROCBASED_CTLS);
    if (enable) {
        controls |= PROCBASED_CTLS_INT_WINDOW_EXITING;
    } else {
        controls &= ~PROCBASED_CTLS_INT_WINDOW_EXITING;
    }
    Write(VmcsField32::PROCBASED_CTLS, controls);
}

static bool has_error_code(uint32_t vector) {
    switch (vector) {
    case X86_INT_DOUBLE_FAULT:
    case X86_INT_INVALID_TSS:
    case X86_INT_SEGMENT_NOT_PRESENT:
    case X86_INT_STACK_FAULT:
    case X86_INT_GP_FAULT:
    case X86_INT_PAGE_FAULT:
    case X86_INT_ALIGNMENT_CHECK:
        return true;
    default:
        return false;
    }
}

void AutoVmcs::IssueInterrupt(uint32_t vector) {
    uint32_t interrupt_info = kInterruptInfoValid | (vector & UINT8_MAX);
    if (vector <= X86_INT_MAX_INTEL_DEFINED)
        interrupt_info |= kInterruptTypeHardwareException;
    if (has_error_code(vector)) {
        interrupt_info |= kInterruptInfoDeliverErrorCode;
        Write(VmcsField32::ENTRY_EXCEPTION_ERROR_CODE, 0);
    }
    Write(VmcsField32::ENTRY_INTERRUPTION_INFORMATION, interrupt_info);
}

uint16_t AutoVmcs::Read(VmcsField16 field) const {
    return static_cast<uint16_t>(vmread(static_cast<uint64_t>(field)));
}

uint32_t AutoVmcs::Read(VmcsField32 field) const {
    return static_cast<uint32_t>(vmread(static_cast<uint64_t>(field)));
}

uint64_t AutoVmcs::Read(VmcsField64 field) const {
    return vmread(static_cast<uint64_t>(field));
}

uint64_t AutoVmcs::Read(VmcsFieldXX field) const {
    return vmread(static_cast<uint64_t>(field));
}

void AutoVmcs::Write(VmcsField16 field, uint16_t val) {
    vmwrite(static_cast<uint64_t>(field), val);
}

void AutoVmcs::Write(VmcsField32 field, uint32_t val) {
    vmwrite(static_cast<uint64_t>(field), val);
}

void AutoVmcs::Write(VmcsField64 field, uint64_t val) {
    vmwrite(static_cast<uint64_t>(field), val);
}

void AutoVmcs::Write(VmcsFieldXX field, uint64_t val) {
    vmwrite(static_cast<uint64_t>(field), val);
}

mx_status_t AutoVmcs::SetControl(VmcsField32 controls, uint64_t true_msr, uint64_t old_msr,
                                 uint32_t set, uint32_t clear) {
    uint32_t allowed_0 = static_cast<uint32_t>(BITS(true_msr, 31, 0));
    uint32_t allowed_1 = static_cast<uint32_t>(BITS_SHIFT(true_msr, 63, 32));
    if ((allowed_1 & set) != set) {
        dprintf(SPEW, "can not set vmcs controls %#x\n", static_cast<uint>(controls));
        return MX_ERR_NOT_SUPPORTED;
    }
    if ((~allowed_0 & clear) != clear) {
        dprintf(SPEW, "can not clear vmcs controls %#x\n", static_cast<uint>(controls));
        return MX_ERR_NOT_SUPPORTED;
    }
    if ((set & clear) != 0) {
        dprintf(SPEW, "can not set and clear the same vmcs controls %#x\n",
                static_cast<uint>(controls));
        return MX_ERR_INVALID_ARGS;
    }

    // Reference Volume 3, Section 31.5.1, Algorithm 3, Part C. If the control
    // can be either 0 or 1 (flexible), and the control is unknown, then refer
    // to the old MSR to find the default value.
    uint32_t flexible = allowed_0 ^ allowed_1;
    uint32_t unknown = flexible & ~(set | clear);
    uint32_t defaults = unknown & BITS(old_msr, 31, 0);
    Write(controls, allowed_0 | defaults | set);
    return MX_OK;
}

static uint cpu_of(uint16_t vpid) {
    return vpid % arch_max_num_cpus();
}

static void pin_thread(thread_t* thread, uint16_t vpid) {
    uint cpu = cpu_of(vpid);
    if (thread_pinned_cpu(thread) != static_cast<int>(cpu))
        thread_set_pinned_cpu(thread, cpu);
    if (arch_curr_cpu_num() != cpu)
        thread_reschedule();
}

static bool check_pinned_cpu_invariant(const thread_t* thread, uint16_t vpid) {
    uint cpu = cpu_of(vpid);
    return thread == get_current_thread() &&
           thread_pinned_cpu(thread) == static_cast<int>(cpu) &&
           arch_curr_cpu_num() == cpu;
}

AutoPin::AutoPin(const Vcpu* vcpu)
    : thread_(get_current_thread()), prev_cpu_(thread_pinned_cpu(thread_)) {
    pin_thread(thread_, vcpu->vpid());
}

AutoPin::~AutoPin() {
    thread_set_pinned_cpu(thread_, prev_cpu_);
}

static uint64_t ept_pointer(paddr_t pml4_address) {
    return
        // Physical address of the PML4 page, page aligned.
        pml4_address |
        // Use write back memory.
        VMX_MEMORY_TYPE_WRITE_BACK << 0 |
        // Page walk length of 4 (defined as N minus 1).
        3u << 3;
}

struct MsrListEntry {
    uint32_t msr;
    uint32_t reserved;
    uint64_t value;
} __PACKED;

static void edit_msr_list(VmxPage* msr_list_page, size_t index, uint32_t msr, uint64_t value) {
    // From Volume 3, Section 24.7.2.

    // From Volume 3, Appendix A.6: Specifically, if the value bits 27:25 of
    // IA32_VMX_MISC is N, then 512 * (N + 1) is the recommended maximum number
    // of MSRs to be included in each list.
    //
    // From Volume 3, Section 24.7.2: This field specifies the number of MSRs to
    // be stored on VM exit. It is recommended that this count not exceed 512
    // bytes.
    //
    // Since these two statements conflict, we are taking the conservative
    // minimum and asserting that: index < (512 bytes / size of MsrListEntry).
    ASSERT(index < (512 / sizeof(MsrListEntry)));

    MsrListEntry* entry = msr_list_page->VirtualAddress<MsrListEntry>() + index;
    entry->msr = msr;
    entry->value = value;
}

mx_status_t vmcs_init(paddr_t vmcs_address, uint16_t vpid, uintptr_t ip, uintptr_t cr3,
                      paddr_t virtual_apic_address, paddr_t apic_access_address,
                      paddr_t msr_bitmaps_address, paddr_t pml4_address, VmxState* vmx_state,
                      VmxPage* host_msr_page, VmxPage* guest_msr_page) {
    mx_status_t status = vmclear(vmcs_address);
    if (status != MX_OK)
        return status;

    AutoVmcs vmcs(vmcs_address);
    // Setup secondary processor-based VMCS controls.
    status = vmcs.SetControl(VmcsField32::PROCBASED_CTLS2,
                             read_msr(X86_MSR_IA32_VMX_PROCBASED_CTLS2),
                             0,
                             // Enable APIC access virtualization.
                             PROCBASED_CTLS2_APIC_ACCESS |
                                 // Enable use of extended page tables.
                                 PROCBASED_CTLS2_EPT |
                                 // Enable use of RDTSCP instruction.
                                 PROCBASED_CTLS2_RDTSCP |
                                 // Associate cached translations of linear
                                 // addresses with a virtual processor ID.
                                 PROCBASED_CTLS2_VPID |
                                 // Enable use of INVPCID instruction.
                                 PROCBASED_CTLS2_INVPCID,
                             0);
    if (status != MX_OK)
        return status;

    // Setup pin-based VMCS controls.
    status = vmcs.SetControl(VmcsField32::PINBASED_CTLS,
                             read_msr(X86_MSR_IA32_VMX_TRUE_PINBASED_CTLS),
                             read_msr(X86_MSR_IA32_VMX_PINBASED_CTLS),
                             // External interrupts cause a VM exit.
                             PINBASED_CTLS_EXT_INT_EXITING |
                                 // Non-maskable interrupts cause a VM exit.
                                 PINBASED_CTLS_NMI_EXITING,
                             0);
    if (status != MX_OK)
        return status;

    // Setup primary processor-based VMCS controls.
    status = vmcs.SetControl(VmcsField32::PROCBASED_CTLS,
                             read_msr(X86_MSR_IA32_VMX_TRUE_PROCBASED_CTLS),
                             read_msr(X86_MSR_IA32_VMX_PROCBASED_CTLS),
                             // Enable VM exit when interrupts are enabled.
                             PROCBASED_CTLS_INT_WINDOW_EXITING |
                                 // Enable VM exit on HLT instruction.
                                 PROCBASED_CTLS_HLT_EXITING |
                                 // Enable TPR virtualization.
                                 PROCBASED_CTLS_TPR_SHADOW |
                                 // Enable VM exit on IO instructions.
                                 PROCBASED_CTLS_IO_EXITING |
                                 // Enable use of MSR bitmaps.
                                 PROCBASED_CTLS_MSR_BITMAPS |
                                 // Enable secondary processor-based controls.
                                 PROCBASED_CTLS_PROCBASED_CTLS2,
                             // Disable VM exit on CR3 load.
                             PROCBASED_CTLS_CR3_LOAD_EXITING |
                                 // Disable VM exit on CR3 store.
                                 PROCBASED_CTLS_CR3_STORE_EXITING |
                                 // Disable VM exit on CR8 load.
                                 PROCBASED_CTLS_CR8_LOAD_EXITING |
                                 // Disable VM exit on CR8 store.
                                 PROCBASED_CTLS_CR8_STORE_EXITING);
    if (status != MX_OK)
        return status;

    // We only enable interrupt-window exiting above to ensure that the
    // processor supports it for later use. So disable it for now.
    vmcs.InterruptWindowExiting(false);

    // Setup VM-exit VMCS controls.
    status = vmcs.SetControl(VmcsField32::EXIT_CTLS,
                             read_msr(X86_MSR_IA32_VMX_TRUE_EXIT_CTLS),
                             read_msr(X86_MSR_IA32_VMX_EXIT_CTLS),
                             // Logical processor is in 64-bit mode after VM
                             // exit. On VM exit CS.L, IA32_EFER.LME, and
                             // IA32_EFER.LMA is set to true.
                             EXIT_CTLS_64BIT_MODE |
                                 // Save the guest IA32_PAT MSR on exit.
                                 EXIT_CTLS_SAVE_IA32_PAT |
                                 // Load the host IA32_PAT MSR on exit.
                                 EXIT_CTLS_LOAD_IA32_PAT |
                                 // Save the guest IA32_EFER MSR on exit.
                                 EXIT_CTLS_SAVE_IA32_EFER |
                                 // Load the host IA32_EFER MSR on exit.
                                 EXIT_CTLS_LOAD_IA32_EFER,
                             0);
    if (status != MX_OK)
        return status;

    // Setup VM-entry VMCS controls.
    status = vmcs.SetControl(VmcsField32::ENTRY_CTLS,
                             read_msr(X86_MSR_IA32_VMX_TRUE_ENTRY_CTLS),
                             read_msr(X86_MSR_IA32_VMX_ENTRY_CTLS),
                             // After VM entry, logical processor is in IA-32e
                             // mode and IA32_EFER.LMA is set to true.
                             ENTRY_CTLS_IA32E_MODE |
                                 // Load the guest IA32_PAT MSR on entry.
                                 ENTRY_CTLS_LOAD_IA32_PAT |
                                 // Load the guest IA32_EFER MSR on entry.
                                 ENTRY_CTLS_LOAD_IA32_EFER,
                             0);
    if (status != MX_OK)
        return status;

    // From Volume 3, Section 24.6.3: The exception bitmap is a 32-bit field
    // that contains one bit for each exception. When an exception occurs,
    // its vector is used to select a bit in this field. If the bit is 1,
    // the exception causes a VM exit. If the bit is 0, the exception is
    // delivered normally through the IDT, using the descriptor
    // corresponding to the exception’s vector.
    //
    // From Volume 3, Section 25.2: If software desires VM exits on all page
    // faults, it can set bit 14 in the exception bitmap to 1 and set the
    // page-fault error-code mask and match fields each to 00000000H.
    vmcs.Write(VmcsField32::EXCEPTION_BITMAP, 0);
    vmcs.Write(VmcsField32::PAGEFAULT_ERRORCODE_MASK, 0);
    vmcs.Write(VmcsField32::PAGEFAULT_ERRORCODE_MATCH, 0);

    // From Volume 3, Section 28.1: Virtual-processor identifiers (VPIDs)
    // introduce to VMX operation a facility by which a logical processor may
    // cache information for multiple linear-address spaces. When VPIDs are
    // used, VMX transitions may retain cached information and the logical
    // processor switches to a different linear-address space.
    //
    // From Volume 3, Section 26.2.1.1: If the “enable VPID” VM-execution
    // control is 1, the value of the VPID VM-execution control field must not
    // be 0000H.
    //
    // From Volume 3, Section 28.3.3.3: If EPT is in use, the logical processor
    // associates all mappings it creates with the value of bits 51:12 of
    // current EPTP. If a VMM uses different EPTP values for different guests,
    // it may use the same VPID for those guests.
    //
    // From Volume 3, Section 28.3.3.1: Operations that architecturally
    // invalidate entries in the TLBs or paging-structure caches independent of
    // VMX operation (e.g., the INVLPG and INVPCID instructions) invalidate
    // linear mappings and combined mappings. They are required to do so only
    // for the current VPID (but, for combined mappings, all EP4TAs). Linear
    // mappings for the current VPID are invalidated even if EPT is in use.
    // Combined mappings for the current VPID are invalidated even if EPT is
    // not in use.
    vmcs.Write(VmcsField16::VPID, vpid);

    // From Volume 3, Section 28.2: The extended page-table mechanism (EPT) is a
    // feature that can be used to support the virtualization of physical
    // memory. When EPT is in use, certain addresses that would normally be
    // treated as physical addresses (and used to access memory) are instead
    // treated as guest-physical addresses. Guest-physical addresses are
    // translated by traversing a set of EPT paging structures to produce
    // physical addresses that are used to access memory.
    vmcs.Write(VmcsField64::EPT_POINTER, ept_pointer(pml4_address));

    // Setup APIC handling.
    vmcs.Write(VmcsField64::APIC_ACCESS_ADDRESS, apic_access_address);
    vmcs.Write(VmcsField64::VIRTUAL_APIC_ADDRESS, virtual_apic_address);

    // Setup MSR handling.
    vmcs.Write(VmcsField64::MSR_BITMAPS_ADDRESS, msr_bitmaps_address);

    edit_msr_list(host_msr_page, 0, X86_MSR_IA32_KERNEL_GS_BASE,
                  read_msr(X86_MSR_IA32_KERNEL_GS_BASE));
    edit_msr_list(host_msr_page, 1, X86_MSR_IA32_STAR, read_msr(X86_MSR_IA32_STAR));
    edit_msr_list(host_msr_page, 2, X86_MSR_IA32_LSTAR, read_msr(X86_MSR_IA32_LSTAR));
    edit_msr_list(host_msr_page, 3, X86_MSR_IA32_FMASK, read_msr(X86_MSR_IA32_FMASK));
    edit_msr_list(host_msr_page, 4, X86_MSR_IA32_TSC_ADJUST, read_msr(X86_MSR_IA32_TSC_ADJUST));
    edit_msr_list(host_msr_page, 5, X86_MSR_IA32_TSC_AUX, read_msr(X86_MSR_IA32_TSC_AUX));

    vmcs.Write(VmcsField64::EXIT_MSR_LOAD_ADDRESS, host_msr_page->PhysicalAddress());
    vmcs.Write(VmcsField32::EXIT_MSR_LOAD_COUNT, 6);

    edit_msr_list(guest_msr_page, 0, X86_MSR_IA32_KERNEL_GS_BASE, 0);
    edit_msr_list(guest_msr_page, 1, X86_MSR_IA32_STAR, 0);
    edit_msr_list(guest_msr_page, 2, X86_MSR_IA32_LSTAR, 0);
    edit_msr_list(guest_msr_page, 3, X86_MSR_IA32_FMASK, 0);
    edit_msr_list(guest_msr_page, 4, X86_MSR_IA32_TSC_ADJUST, 0);
    edit_msr_list(guest_msr_page, 5, X86_MSR_IA32_TSC_AUX, 0);
    vmcs.Write(VmcsField64::EXIT_MSR_STORE_ADDRESS, guest_msr_page->PhysicalAddress());
    vmcs.Write(VmcsField32::EXIT_MSR_STORE_COUNT, 6);
    vmcs.Write(VmcsField64::ENTRY_MSR_LOAD_ADDRESS, guest_msr_page->PhysicalAddress());
    vmcs.Write(VmcsField32::ENTRY_MSR_LOAD_COUNT, 6);

    // Setup VMCS host state.
    //
    // NOTE: We are pinned to a thread when executing this function, therefore
    // it is acceptable to use per-CPU state.
    x86_percpu* percpu = x86_get_percpu();
    vmcs.Write(VmcsField64::HOST_IA32_PAT, read_msr(X86_MSR_IA32_PAT));
    vmcs.Write(VmcsField64::HOST_IA32_EFER, read_msr(X86_MSR_IA32_EFER));
    vmcs.Write(VmcsFieldXX::HOST_CR0, x86_get_cr0());
    vmcs.Write(VmcsFieldXX::HOST_CR3, x86_get_cr3());
    vmcs.Write(VmcsFieldXX::HOST_CR4, x86_get_cr4());
    vmcs.Write(VmcsField16::HOST_ES_SELECTOR, 0);
    vmcs.Write(VmcsField16::HOST_CS_SELECTOR, CODE_64_SELECTOR);
    vmcs.Write(VmcsField16::HOST_SS_SELECTOR, DATA_SELECTOR);
    vmcs.Write(VmcsField16::HOST_DS_SELECTOR, 0);
    vmcs.Write(VmcsField16::HOST_FS_SELECTOR, 0);
    vmcs.Write(VmcsField16::HOST_GS_SELECTOR, 0);
    vmcs.Write(VmcsField16::HOST_TR_SELECTOR, TSS_SELECTOR(percpu->cpu_num));
    vmcs.Write(VmcsFieldXX::HOST_FS_BASE, read_msr(X86_MSR_IA32_FS_BASE));
    vmcs.Write(VmcsFieldXX::HOST_GS_BASE, read_msr(X86_MSR_IA32_GS_BASE));
    vmcs.Write(VmcsFieldXX::HOST_TR_BASE, reinterpret_cast<uint64_t>(&percpu->default_tss));
    vmcs.Write(VmcsFieldXX::HOST_GDTR_BASE, reinterpret_cast<uint64_t>(_gdt));
    vmcs.Write(VmcsFieldXX::HOST_IDTR_BASE, reinterpret_cast<uint64_t>(idt_get_readonly()));
    vmcs.Write(VmcsFieldXX::HOST_IA32_SYSENTER_ESP, 0);
    vmcs.Write(VmcsFieldXX::HOST_IA32_SYSENTER_EIP, 0);
    vmcs.Write(VmcsField32::HOST_IA32_SYSENTER_CS, 0);
    vmcs.Write(VmcsFieldXX::HOST_RSP, reinterpret_cast<uint64_t>(vmx_state));
    vmcs.Write(VmcsFieldXX::HOST_RIP, reinterpret_cast<uint64_t>(vmx_exit_entry));

    // Setup VMCS guest state.
    uint64_t cr0 = X86_CR0_PE | // Enable protected mode
                   X86_CR0_PG | // Enable paging
                   X86_CR0_NE; // Enable internal x87 exception handling
    if (cr_is_invalid(cr0, X86_MSR_IA32_VMX_CR0_FIXED0, X86_MSR_IA32_VMX_CR0_FIXED1)) {
        return MX_ERR_BAD_STATE;
    }
    vmcs.Write(VmcsFieldXX::GUEST_CR0, cr0);

    uint64_t cr4 = X86_CR4_PAE | // Enable PAE paging
                   X86_CR4_VMXE; // Enable VMX
    if (cr_is_invalid(cr4, X86_MSR_IA32_VMX_CR4_FIXED0, X86_MSR_IA32_VMX_CR4_FIXED1)) {
        return MX_ERR_BAD_STATE;
    }
    vmcs.Write(VmcsFieldXX::GUEST_CR4, cr4);

    // For now, the guest can own all of the CR4 bits except VMXE, which it shouldn't touch.
    // TODO(andymutton): Implement proper CR4 handling.
    vmcs.Write(VmcsFieldXX::CR4_GUEST_HOST_MASK, X86_CR4_VMXE);
    vmcs.Write(VmcsFieldXX::CR4_READ_SHADOW, 0);

    vmcs.Write(VmcsField64::GUEST_IA32_PAT, read_msr(X86_MSR_IA32_PAT));
    vmcs.Write(VmcsField64::GUEST_IA32_EFER, read_msr(X86_MSR_IA32_EFER));

    vmcs.Write(VmcsField32::GUEST_CS_ACCESS_RIGHTS,
               GUEST_XX_ACCESS_RIGHTS_TYPE_A |
                   GUEST_XX_ACCESS_RIGHTS_TYPE_W |
                   GUEST_XX_ACCESS_RIGHTS_TYPE_E |
                   GUEST_XX_ACCESS_RIGHTS_TYPE_CODE |
                   GUEST_XX_ACCESS_RIGHTS_S |
                   GUEST_XX_ACCESS_RIGHTS_P |
                   GUEST_XX_ACCESS_RIGHTS_L);

    vmcs.Write(VmcsField32::GUEST_TR_ACCESS_RIGHTS,
               GUEST_TR_ACCESS_RIGHTS_TSS_BUSY |
                   GUEST_XX_ACCESS_RIGHTS_P);

    // Disable all other segment selectors until we have a guest that uses them.
    vmcs.Write(VmcsField32::GUEST_SS_ACCESS_RIGHTS, GUEST_XX_ACCESS_RIGHTS_UNUSABLE);
    vmcs.Write(VmcsField32::GUEST_DS_ACCESS_RIGHTS, GUEST_XX_ACCESS_RIGHTS_UNUSABLE);
    vmcs.Write(VmcsField32::GUEST_ES_ACCESS_RIGHTS, GUEST_XX_ACCESS_RIGHTS_UNUSABLE);
    vmcs.Write(VmcsField32::GUEST_FS_ACCESS_RIGHTS, GUEST_XX_ACCESS_RIGHTS_UNUSABLE);
    vmcs.Write(VmcsField32::GUEST_GS_ACCESS_RIGHTS, GUEST_XX_ACCESS_RIGHTS_UNUSABLE);
    vmcs.Write(VmcsField32::GUEST_LDTR_ACCESS_RIGHTS, GUEST_XX_ACCESS_RIGHTS_UNUSABLE);

    vmcs.Write(VmcsFieldXX::GUEST_GDTR_BASE, 0);
    vmcs.Write(VmcsField32::GUEST_GDTR_LIMIT, 0);
    vmcs.Write(VmcsFieldXX::GUEST_IDTR_BASE, 0);
    vmcs.Write(VmcsField32::GUEST_IDTR_LIMIT, 0);

    // Set all reserved RFLAGS bits to their correct values
    vmcs.Write(VmcsFieldXX::GUEST_RFLAGS, X86_FLAGS_RESERVED_ONES);

    vmcs.Write(VmcsField32::GUEST_ACTIVITY_STATE, 0);
    vmcs.Write(VmcsField32::GUEST_INTERRUPTIBILITY_STATE, 0);
    vmcs.Write(VmcsFieldXX::GUEST_PENDING_DEBUG_EXCEPTIONS, 0);

    // From Volume 3, Section 26.3.1.1: The IA32_SYSENTER_ESP field and the
    // IA32_SYSENTER_EIP field must each contain a canonical address.
    vmcs.Write(VmcsFieldXX::GUEST_IA32_SYSENTER_ESP, 0);
    vmcs.Write(VmcsFieldXX::GUEST_IA32_SYSENTER_EIP, 0);
    vmcs.Write(VmcsField32::GUEST_IA32_SYSENTER_CS, 0);

    vmcs.Write(VmcsFieldXX::GUEST_RSP, 0);
    vmcs.Write(VmcsFieldXX::GUEST_RIP, ip);
    vmcs.Write(VmcsFieldXX::GUEST_CR3, cr3);

    // From Volume 3, Section 24.4.2: If the “VMCS shadowing” VM-execution
    // control is 1, the VMREAD and VMWRITE instructions access the VMCS
    // referenced by this pointer (see Section 24.10). Otherwise, software
    // should set this field to FFFFFFFF_FFFFFFFFH to avoid VM-entry
    // failures (see Section 26.3.1.5).
    vmcs.Write(VmcsField64::LINK_POINTER, LINK_POINTER_INVALIDATE);

    if (x86_feature_test(X86_FEATURE_XSAVE)) {
        // Enable x87 state in guest XCR0.
        vmx_state->guest_state.xcr0 = X86_XSAVE_STATE_X87;
    }

    return MX_OK;
}

// static
mx_status_t Vcpu::Create(mx_vaddr_t ip, mx_vaddr_t cr3, fbl::RefPtr<VmObject> apic_vmo,
                         paddr_t apic_access_address, paddr_t msr_bitmaps_address,
                         GuestPhysicalAddressSpace* gpas, PacketMux& mux,
                         fbl::unique_ptr<Vcpu>* out) {
    uint16_t vpid;
    mx_status_t status = alloc_vpid(&vpid);
    if (status != MX_OK)
        return status;
    auto auto_call = fbl::MakeAutoCall([=]() { free_vpid(vpid); });

    // When we create a VCPU, we bind it to the current thread and a CPU based
    // on the VPID. The VCPU must always be run on the current thread and the
    // given CPU, unless an explicit migration is performed.
    //
    // The reason we do this is that:
    // 1. The state of the current thread is stored within the VMCS, to be
    //    restored upon a guest-to-host transition.
    // 2. The state of the VMCS associated with the VCPU is cached within the
    //    CPU. To move to a different CPU, we must perform an explicit migration
    //    which will cost us performance.
    thread_t* thread = get_current_thread();
    pin_thread(thread, vpid);

    fbl::AllocChecker ac;
    fbl::unique_ptr<Vcpu> vcpu(new (&ac) Vcpu(thread, vpid, apic_vmo, gpas, mux));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    timer_init(&vcpu->local_apic_state_.timer);
    event_init(&vcpu->local_apic_state_.event, false, EVENT_FLAG_AUTOUNSIGNAL);
    status = vcpu->local_apic_state_.interrupt_bitmap.Reset(kNumInterrupts);
    if (status != MX_OK)
        return status;

    paddr_t virtual_apic_address;
    status = vcpu->apic_vmo_->Lookup(0, PAGE_SIZE, kPfFlags, guest_lookup_page,
                                     &virtual_apic_address);
    if (status != MX_OK)
        return status;
    vcpu->local_apic_state_.apic_addr = paddr_to_kvaddr(virtual_apic_address);

    VmxInfo vmx_info;
    status = vcpu->host_msr_page_.Alloc(vmx_info, 0);
    if (status != MX_OK)
        return status;

    status = vcpu->guest_msr_page_.Alloc(vmx_info, 0);
    if (status != MX_OK)
        return status;

    status = vcpu->vmcs_page_.Alloc(vmx_info, 0);
    if (status != MX_OK)
        return status;

    VmxRegion* region = vcpu->vmcs_page_.VirtualAddress<VmxRegion>();
    region->revision_id = vmx_info.revision_id;
    status = vmcs_init(vcpu->vmcs_page_.PhysicalAddress(), vpid, ip, cr3, virtual_apic_address,
                       apic_access_address, msr_bitmaps_address, gpas->Pml4Address(),
                       &vcpu->vmx_state_, &vcpu->host_msr_page_, &vcpu->guest_msr_page_);
    if (status != MX_OK)
        return status;

    auto_call.cancel();
    *out = fbl::move(vcpu);
    return MX_OK;
}

Vcpu::Vcpu(const thread_t* thread, uint16_t vpid, fbl::RefPtr<VmObject> apic_vmo,
           GuestPhysicalAddressSpace* gpas, PacketMux& mux)
    : thread_(thread), vpid_(vpid), apic_vmo_(apic_vmo), gpas_(gpas), mux_(mux),
      vmx_state_(/* zero-init */) {}

Vcpu::~Vcpu() {
    if (!vmcs_page_.IsAllocated())
        return;

    // The destructor may be called from a different thread, therefore we must
    // pin the current thread to the same CPU as the VCPU.
    AutoPin pin(this);
    vmclear(vmcs_page_.PhysicalAddress());
    __UNUSED mx_status_t status = free_vpid(vpid_);
    DEBUG_ASSERT(status == MX_OK);
}

mx_status_t Vcpu::Resume(mx_port_packet_t* packet) {
    if (!check_pinned_cpu_invariant(thread_, vpid_))
        return MX_ERR_BAD_STATE;
    mx_status_t status;
    do {
        AutoVmcs vmcs(vmcs_page_.PhysicalAddress());
        if (x86_feature_test(X86_FEATURE_XSAVE)) {
            // Save the host XCR0, and load the guest XCR0.
            vmx_state_.host_state.xcr0 = x86_xgetbv(0);
            x86_xsetbv(0, vmx_state_.guest_state.xcr0);
        }
        status = vmx_enter(&vmx_state_);
        if (x86_feature_test(X86_FEATURE_XSAVE)) {
            // Save the guest XCR0, and load the host XCR0.
            vmx_state_.guest_state.xcr0 = x86_xgetbv(0);
            x86_xsetbv(0, vmx_state_.host_state.xcr0);
        }
        if (status != MX_OK) {
            uint64_t error = vmcs.Read(VmcsField32::INSTRUCTION_ERROR);
            dprintf(SPEW, "vmlaunch failed: %#" PRIx64 "\n", error);
        } else {
            vmx_state_.resume = true;
            GuestState* guest_state = &vmx_state_.guest_state;
            status = vmexit_handler(&vmcs, guest_state, &local_apic_state_, gpas_, mux_, packet);
        }
    } while (status == MX_OK);
    return status == MX_ERR_NEXT ? MX_OK : status;
}

void vmx_exit(VmxState* vmx_state) {
    DEBUG_ASSERT(arch_ints_disabled());

    // Reload the task segment in order to restore its limit. VMX always
    // restores it with a limit of 0x67, which excludes the IO bitmap.
    seg_sel_t selector = TSS_SELECTOR(arch_curr_cpu_num());
    x86_clear_tss_busy(selector);
    x86_ltr(selector);

    // Reload the interrupt descriptor table in order to restore its limit. VMX
    // always restores it with a limit of 0xffff, which is too large.
    idt_load(idt_get_readonly());
}

mx_status_t Vcpu::Interrupt(uint32_t vector) {
    if (vector > X86_MAX_INT)
        return MX_ERR_OUT_OF_RANGE;
    if (!local_apic_signal_interrupt(&local_apic_state_, vector, true)) {
        // If we did not signal the VCPU, it means it is currently running,
        // therefore we should issue an IPI to force a VM exit.
        mp_reschedule(MP_IPI_TARGET_MASK, 1u << cpu_of(vpid_), 0);
    }
    return MX_OK;
}

template <typename Out, typename In>
static void register_copy(Out* out, const In& in) {
    out->rax = in.rax;
    out->rcx = in.rcx;
    out->rdx = in.rdx;
    out->rbx = in.rbx;
    out->rbp = in.rbp;
    out->rsi = in.rsi;
    out->rdi = in.rdi;
    out->r8 = in.r8;
    out->r9 = in.r9;
    out->r10 = in.r10;
    out->r11 = in.r11;
    out->r12 = in.r12;
    out->r13 = in.r13;
    out->r14 = in.r14;
    out->r15 = in.r15;
}

mx_status_t Vcpu::ReadState(uint32_t kind, void* buffer, uint32_t len) const {
    if (!check_pinned_cpu_invariant(thread_, vpid_))
        return MX_ERR_BAD_STATE;
    switch (kind) {
    case MX_VCPU_STATE: {
        if (len != sizeof(mx_vcpu_state_t))
            break;
        auto state = static_cast<mx_vcpu_state_t*>(buffer);
        register_copy(state, vmx_state_.guest_state);
        AutoVmcs vmcs(vmcs_page_.PhysicalAddress());
        state->rsp = vmcs.Read(VmcsFieldXX::GUEST_RSP);
        state->flags = vmcs.Read(VmcsFieldXX::GUEST_RFLAGS) & X86_FLAGS_USER;
        return MX_OK;
    }
    }
    return MX_ERR_INVALID_ARGS;
}

mx_status_t Vcpu::WriteState(uint32_t kind, const void* buffer, uint32_t len) {
    if (!check_pinned_cpu_invariant(thread_, vpid_))
        return MX_ERR_BAD_STATE;
    switch (kind) {
    case MX_VCPU_STATE: {
        if (len != sizeof(mx_vcpu_state_t))
            break;
        auto state = static_cast<const mx_vcpu_state_t*>(buffer);
        register_copy(&vmx_state_.guest_state, *state);
        AutoVmcs vmcs(vmcs_page_.PhysicalAddress());
        vmcs.Write(VmcsFieldXX::GUEST_RSP, state->rsp);
        if (state->flags & X86_FLAGS_RESERVED_ONES) {
            const uint64_t rflags = vmcs.Read(VmcsFieldXX::GUEST_RFLAGS);
            const uint64_t user_flags = (rflags & ~X86_FLAGS_USER) |
                                        (state->flags & X86_FLAGS_USER);
            vmcs.Write(VmcsFieldXX::GUEST_RFLAGS, user_flags);
        }
        return MX_OK;
    }
    case MX_VCPU_IO: {
        if (len != sizeof(mx_vcpu_io_t))
            break;
        auto io = static_cast<const mx_vcpu_io_t*>(buffer);
        memcpy(&vmx_state_.guest_state.rax, io->data, io->access_size);
        return MX_OK;
    }
    }
    return MX_ERR_INVALID_ARGS;
}

mx_status_t x86_vcpu_create(mx_vaddr_t ip, mx_vaddr_t cr3, fbl::RefPtr<VmObject> apic_vmo,
                            paddr_t apic_access_address, paddr_t msr_bitmaps_address,
                            GuestPhysicalAddressSpace* gpas, PacketMux& mux,
                            fbl::unique_ptr<Vcpu>* out) {
    return Vcpu::Create(ip, cr3, apic_vmo, apic_access_address, msr_bitmaps_address, gpas, mux,
                        out);
}

mx_status_t arch_vcpu_resume(Vcpu* vcpu, mx_port_packet_t* packet) {
    return vcpu->Resume(packet);
}

mx_status_t arch_vcpu_interrupt(Vcpu* vcpu, uint32_t vector) {
    return vcpu->Interrupt(vector);
}

mx_status_t arch_vcpu_read_state(const Vcpu* vcpu, uint32_t kind, void* buffer, uint32_t len) {
    return vcpu->ReadState(kind, buffer, len);
}

mx_status_t arch_vcpu_write_state(Vcpu* vcpu, uint32_t kind, const void* buffer, uint32_t len) {
    return vcpu->WriteState(kind, buffer, len);
}
