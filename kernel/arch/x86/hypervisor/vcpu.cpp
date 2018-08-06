// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>

#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/x86/pvclock.h>
#include <fbl/auto_call.h>
#include <hypervisor/cpu.h>
#include <hypervisor/ktrace.h>
#include <kernel/mp.h>
#include <lib/ktrace.h>
#include <vm/fault.h>
#include <vm/pmm.h>
#include <vm/vm_object.h>
#include <zircon/syscalls/hypervisor.h>

#include "pvclock_priv.h"
#include "vcpu_priv.h"
#include "vmexit_priv.h"
#include "vmx_cpu_state_priv.h"

static constexpr uint32_t kInterruptInfoValid = 1u << 31;
static constexpr uint32_t kInterruptInfoDeliverErrorCode = 1u << 11;
static constexpr uint32_t kInterruptTypeHardwareException = 3u << 8;
static constexpr uint32_t kInterruptTypeSoftwareException = 6u << 8;
static constexpr uint16_t kBaseProcessorVpid = 1;

static zx_status_t invept(InvEpt invalidation, uint64_t eptp) {
    uint8_t err;
    uint64_t descriptor[] = {eptp, 0};

    __asm__ volatile(
        "invept %[descriptor], %[invalidation];" VMX_ERR_CHECK(err)
        : [err] "=r"(err)
        : [descriptor] "m"(descriptor), [invalidation] "r"(invalidation)
        : "cc");

    return err ? ZX_ERR_INTERNAL : ZX_OK;
}

static zx_status_t vmptrld(paddr_t pa) {
    uint8_t err;

    __asm__ volatile(
        "vmptrld %[pa];" VMX_ERR_CHECK(err)
        : [err] "=r"(err)
        : [pa] "m"(pa)
        : "cc", "memory");

    return err ? ZX_ERR_INTERNAL : ZX_OK;
}

static zx_status_t vmclear(paddr_t pa) {
    uint8_t err;

    __asm__ volatile(
        "vmclear %[pa];" VMX_ERR_CHECK(err)
        : [err] "=r"(err)
        : [pa] "m"(pa)
        : "cc", "memory");

    return err ? ZX_ERR_INTERNAL : ZX_OK;
}

static uint64_t vmread(uint64_t field) {
    uint8_t err;
    uint64_t val;

    __asm__ volatile(
        "vmread %[field], %[val];" VMX_ERR_CHECK(err)
        : [err] "=r"(err), [val] "=m"(val)
        : [field] "r"(field)
        : "cc");

    DEBUG_ASSERT(err == ZX_OK);
    return val;
}

static void vmwrite(uint64_t field, uint64_t val) {
    uint8_t err;

    __asm__ volatile(
        "vmwrite %[val], %[field];" VMX_ERR_CHECK(err)
        : [err] "=r"(err)
        : [val] "r"(val), [field] "r"(field)
        : "cc");

    DEBUG_ASSERT(err == ZX_OK);
}

AutoVmcs::AutoVmcs(paddr_t vmcs_address)
    : vmcs_address_(vmcs_address) {
    DEBUG_ASSERT(!arch_ints_disabled());
    arch_disable_ints();
    __UNUSED zx_status_t status = vmptrld(vmcs_address_);
    DEBUG_ASSERT(status == ZX_OK);
}

AutoVmcs::~AutoVmcs() {
    DEBUG_ASSERT(arch_ints_disabled());
    arch_enable_ints();
}

void AutoVmcs::Invalidate() {
#if LK_DEBUGLEVEL > 0
    vmcs_address_ = 0;
#endif
}

void AutoVmcs::InterruptWindowExiting(bool enable) {
    DEBUG_ASSERT(vmcs_address_ != 0);
    uint32_t controls = Read(VmcsField32::PROCBASED_CTLS);
    if (enable) {
        controls |= kProcbasedCtlsIntWindowExiting;
    } else {
        controls &= ~kProcbasedCtlsIntWindowExiting;
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
    DEBUG_ASSERT(vmcs_address_ != 0);
    uint32_t interrupt_info = kInterruptInfoValid | (vector & UINT8_MAX);
    if (vector == X86_INT_BREAKPOINT || vector == X86_INT_OVERFLOW) {
        // From Volume 3, Section 24.8.3. A VMM should use type hardware exception for all
        // exceptions other than breakpoints and overflows, which should be software exceptions.
        interrupt_info |= kInterruptTypeSoftwareException;
    } else if (vector < X86_INT_PLATFORM_BASE) {
        // From Volume 3, Section 6.15. Vectors from 0 to 32 (X86_INT_PLATFORM_BASE) are exceptions.
        interrupt_info |= kInterruptTypeHardwareException;
    }
    if (has_error_code(vector)) {
        interrupt_info |= kInterruptInfoDeliverErrorCode;
        Write(VmcsField32::ENTRY_EXCEPTION_ERROR_CODE, 0);
    }

    DEBUG_ASSERT((Read(VmcsField32::ENTRY_INTERRUPTION_INFORMATION) & kInterruptInfoValid) == 0);
    Write(VmcsField32::ENTRY_INTERRUPTION_INFORMATION, interrupt_info);
}

uint16_t AutoVmcs::Read(VmcsField16 field) const {
    DEBUG_ASSERT(vmcs_address_ != 0);
    return static_cast<uint16_t>(vmread(static_cast<uint64_t>(field)));
}

uint32_t AutoVmcs::Read(VmcsField32 field) const {
    DEBUG_ASSERT(vmcs_address_ != 0);
    return static_cast<uint32_t>(vmread(static_cast<uint64_t>(field)));
}

uint64_t AutoVmcs::Read(VmcsField64 field) const {
    DEBUG_ASSERT(vmcs_address_ != 0);
    return vmread(static_cast<uint64_t>(field));
}

uint64_t AutoVmcs::Read(VmcsFieldXX field) const {
    DEBUG_ASSERT(vmcs_address_ != 0);
    return vmread(static_cast<uint64_t>(field));
}

void AutoVmcs::Write(VmcsField16 field, uint16_t val) {
    DEBUG_ASSERT(vmcs_address_ != 0);
    vmwrite(static_cast<uint64_t>(field), val);
}

void AutoVmcs::Write(VmcsField32 field, uint32_t val) {
    DEBUG_ASSERT(vmcs_address_ != 0);
    vmwrite(static_cast<uint64_t>(field), val);
}

void AutoVmcs::Write(VmcsField64 field, uint64_t val) {
    DEBUG_ASSERT(vmcs_address_ != 0);
    vmwrite(static_cast<uint64_t>(field), val);
}

void AutoVmcs::Write(VmcsFieldXX field, uint64_t val) {
    DEBUG_ASSERT(vmcs_address_ != 0);
    vmwrite(static_cast<uint64_t>(field), val);
}

zx_status_t AutoVmcs::SetControl(VmcsField32 controls, uint64_t true_msr, uint64_t old_msr,
                                 uint32_t set, uint32_t clear) {
    DEBUG_ASSERT(vmcs_address_ != 0);
    uint32_t allowed_0 = static_cast<uint32_t>(BITS(true_msr, 31, 0));
    uint32_t allowed_1 = static_cast<uint32_t>(BITS_SHIFT(true_msr, 63, 32));
    if ((allowed_1 & set) != set) {
        dprintf(INFO, "can not set vmcs controls %#x\n", static_cast<uint>(controls));
        return ZX_ERR_NOT_SUPPORTED;
    }
    if ((~allowed_0 & clear) != clear) {
        dprintf(INFO, "can not clear vmcs controls %#x\n", static_cast<uint>(controls));
        return ZX_ERR_NOT_SUPPORTED;
    }
    if ((set & clear) != 0) {
        dprintf(INFO, "can not set and clear the same vmcs controls %#x\n",
                static_cast<uint>(controls));
        return ZX_ERR_INVALID_ARGS;
    }

    // See Volume 3, Section 31.5.1, Algorithm 3, Part C. If the control can be
    // either 0 or 1 (flexible), and the control is unknown, then refer to the
    // old MSR to find the default value.
    uint32_t flexible = allowed_0 ^ allowed_1;
    uint32_t unknown = flexible & ~(set | clear);
    uint32_t defaults = unknown & BITS(old_msr, 31, 0);
    Write(controls, allowed_0 | defaults | set);
    return ZX_OK;
}

AutoPin::AutoPin(uint16_t vpid)
    : prev_cpu_mask_(get_current_thread()->cpu_affinity), thread_(hypervisor::pin_thread(vpid)) {}

AutoPin::~AutoPin() {
    thread_set_cpu_affinity(thread_, prev_cpu_mask_);
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

static zx_status_t vmcs_init(paddr_t vmcs_address, uint16_t vpid, uintptr_t entry,
                      paddr_t msr_bitmaps_address, paddr_t pml4_address, VmxState* vmx_state,
                      VmxPage* host_msr_page, VmxPage* guest_msr_page) {
    zx_status_t status = vmclear(vmcs_address);
    if (status != ZX_OK)
        return status;

    AutoVmcs vmcs(vmcs_address);
    // Setup secondary processor-based VMCS controls.
    status = vmcs.SetControl(VmcsField32::PROCBASED_CTLS2,
                             read_msr(X86_MSR_IA32_VMX_PROCBASED_CTLS2),
                             0,
                             // Enable use of extended page tables.
                             kProcbasedCtls2Ept |
                                 // Enable use of RDTSCP instruction.
                                 kProcbasedCtls2Rdtscp |
                                 // Enable X2APIC.
                                 kProcbasedCtls2x2Apic |
                                 // Associate cached translations of linear
                                 // addresses with a virtual processor ID.
                                 kProcbasedCtls2Vpid |
                                 // Enable unrestricted guest.
                                 kProcbasedCtls2UnrestrictedGuest,
                             0);
    if (status != ZX_OK)
        return status;

    // Enable use of INVPCID instruction if available.
    vmcs.SetControl(VmcsField32::PROCBASED_CTLS2,
                    read_msr(X86_MSR_IA32_VMX_PROCBASED_CTLS2),
                    vmcs.Read(VmcsField32::PROCBASED_CTLS2),
                    kProcbasedCtls2Invpcid,
                    0);

    // Setup pin-based VMCS controls.
    status = vmcs.SetControl(VmcsField32::PINBASED_CTLS,
                             read_msr(X86_MSR_IA32_VMX_TRUE_PINBASED_CTLS),
                             read_msr(X86_MSR_IA32_VMX_PINBASED_CTLS),
                             // External interrupts cause a VM exit.
                             kPinbasedCtlsExtIntExiting |
                                 // Non-maskable interrupts cause a VM exit.
                                 kPinbasedCtlsNmiExiting,
                             0);
    if (status != ZX_OK)
        return status;

    // Setup primary processor-based VMCS controls.
    status = vmcs.SetControl(VmcsField32::PROCBASED_CTLS,
                             read_msr(X86_MSR_IA32_VMX_TRUE_PROCBASED_CTLS),
                             read_msr(X86_MSR_IA32_VMX_PROCBASED_CTLS),
                             // Enable VM exit when interrupts are enabled.
                             kProcbasedCtlsIntWindowExiting |
                                 // Enable VM exit on HLT instruction.
                                 kProcbasedCtlsHltExiting |
                                 // Enable TPR virtualization.
                                 kProcbasedCtlsTprShadow |
                                 // Enable VM exit on IO instructions.
                                 kProcbasedCtlsIoExiting |
                                 // Enable use of MSR bitmaps.
                                 kProcbasedCtlsMsrBitmaps |
                                 // Enable VM exit on pause instruction.
                                 kProcbasedCtlsPauseExiting |
                                 // Enable secondary processor-based controls.
                                 kProcbasedCtlsProcbasedCtls2,
                             // Disable VM exit on CR3 load.
                             kProcbasedCtlsCr3LoadExiting |
                                 // Disable VM exit on CR3 store.
                                 kProcbasedCtlsCr3StoreExiting |
                                 // Disable VM exit on CR8 load.
                                 kProcbasedCtlsCr8LoadExiting |
                                 // Disable VM exit on CR8 store.
                                 kProcbasedCtlsCr8StoreExiting);
    if (status != ZX_OK)
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
                             kExitCtls64bitMode |
                                 // Save the guest IA32_PAT MSR on exit.
                                 kExitCtlsSaveIa32Pat |
                                 // Load the host IA32_PAT MSR on exit.
                                 kExitCtlsLoadIa32Pat |
                                 // Save the guest IA32_EFER MSR on exit.
                                 kExitCtlsSaveIa32Efer |
                                 // Load the host IA32_EFER MSR on exit.
                                 kExitCtlsLoadIa32Efer |
                                 // Acknowledge external interrupt on exit.
                                 kExitCtlsAckIntOnExit,
                             0);
    if (status != ZX_OK)
        return status;

    // Setup VM-entry VMCS controls.
    // Load the guest IA32_PAT MSR and IA32_EFER MSR on entry.
    uint32_t entry_ctls = kEntryCtlsLoadIa32Pat | kEntryCtlsLoadIa32Efer;
    if (vpid == kBaseProcessorVpid) {
        // On the BSP, go straight to IA32E mode on entry.
        entry_ctls |= kEntryCtlsIa32eMode;
    }
    status = vmcs.SetControl(VmcsField32::ENTRY_CTLS,
                             read_msr(X86_MSR_IA32_VMX_TRUE_ENTRY_CTLS),
                             read_msr(X86_MSR_IA32_VMX_ENTRY_CTLS),
                             entry_ctls, 0);
    if (status != ZX_OK)
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
    const auto eptp = ept_pointer(pml4_address);
    vmcs.Write(VmcsField64::EPT_POINTER, eptp);

    // From Volume 3, Section 28.3.3.4: Software can use an INVEPT with type all
    // ALL_CONTEXT to prevent undesired retention of cached EPT information. Here,
    // we only care about invalidating information associated with this EPTP.
    invept(InvEpt::SINGLE_CONTEXT, eptp);

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
    vmcs.Write(VmcsFieldXX::HOST_GDTR_BASE, reinterpret_cast<uint64_t>(gdt_get()));
    vmcs.Write(VmcsFieldXX::HOST_IDTR_BASE, reinterpret_cast<uint64_t>(idt_get_readonly()));
    vmcs.Write(VmcsFieldXX::HOST_IA32_SYSENTER_ESP, 0);
    vmcs.Write(VmcsFieldXX::HOST_IA32_SYSENTER_EIP, 0);
    vmcs.Write(VmcsField32::HOST_IA32_SYSENTER_CS, 0);
    vmcs.Write(VmcsFieldXX::HOST_RSP, reinterpret_cast<uint64_t>(vmx_state));
    vmcs.Write(VmcsFieldXX::HOST_RIP, reinterpret_cast<uint64_t>(vmx_exit_entry));

    // Setup VMCS guest state.
    uint64_t cr0 = X86_CR0_PE | // Enable protected mode
                   X86_CR0_PG | // Enable paging
                   X86_CR0_NE;  // Enable internal x87 exception handling
    if (vpid != kBaseProcessorVpid) {
        // Disable protected mode and paging on secondary VCPUs.
        cr0 &= ~(X86_CR0_PE | X86_CR0_PG);
    }
    if (cr0_is_invalid(&vmcs, cr0)) {
        return ZX_ERR_BAD_STATE;
    }
    vmcs.Write(VmcsFieldXX::GUEST_CR0, cr0);

    // Ensure that CR0.NE remains set by masking and manually handling writes to CR0 that unset it.
    vmcs.Write(VmcsFieldXX::CR0_GUEST_HOST_MASK, X86_CR0_NE);
    vmcs.Write(VmcsFieldXX::CR0_READ_SHADOW, X86_CR0_NE);

    uint64_t cr4 = X86_CR4_VMXE; // Enable VMX
    if (vpid == kBaseProcessorVpid) {
        // Enable the PAE bit on the BSP for 64-bit paging.
        cr4 |= X86_CR4_PAE;
    }
    if (cr_is_invalid(cr4, X86_MSR_IA32_VMX_CR4_FIXED0, X86_MSR_IA32_VMX_CR4_FIXED1)) {
        return ZX_ERR_BAD_STATE;
    }
    vmcs.Write(VmcsFieldXX::GUEST_CR4, cr4);

    // For now, the guest can own all of the CR4 bits except VMXE, which it shouldn't touch.
    // TODO(andymutton): Implement proper CR4 handling.
    vmcs.Write(VmcsFieldXX::CR4_GUEST_HOST_MASK, X86_CR4_VMXE);
    vmcs.Write(VmcsFieldXX::CR4_READ_SHADOW, 0);

    vmcs.Write(VmcsField64::GUEST_IA32_PAT, read_msr(X86_MSR_IA32_PAT));

    uint64_t guest_efer = read_msr(X86_MSR_IA32_EFER);
    if (vpid != kBaseProcessorVpid) {
        // Disable LME and LMA on all but the BSP.
        guest_efer &= ~(X86_EFER_LME | X86_EFER_LMA);
    }
    vmcs.Write(VmcsField64::GUEST_IA32_EFER, guest_efer);

    uint32_t cs_access_rights = kGuestXxAccessRightsDefault |
                                kGuestXxAccessRightsTypeE |
                                kGuestXxAccessRightsTypeCode;
    if (vpid == kBaseProcessorVpid) {
        // Ensure that the BSP starts with a 64-bit code segment.
        cs_access_rights |= kGuestXxAccessRightsL;
    }
    vmcs.Write(VmcsField32::GUEST_CS_ACCESS_RIGHTS, cs_access_rights);

    vmcs.Write(VmcsField32::GUEST_TR_ACCESS_RIGHTS,
               kGuestTrAccessRightsTssBusy | kGuestXxAccessRightsP);

    vmcs.Write(VmcsField32::GUEST_SS_ACCESS_RIGHTS, kGuestXxAccessRightsDefault);
    vmcs.Write(VmcsField32::GUEST_DS_ACCESS_RIGHTS, kGuestXxAccessRightsDefault);
    vmcs.Write(VmcsField32::GUEST_ES_ACCESS_RIGHTS, kGuestXxAccessRightsDefault);
    vmcs.Write(VmcsField32::GUEST_FS_ACCESS_RIGHTS, kGuestXxAccessRightsDefault);
    vmcs.Write(VmcsField32::GUEST_GS_ACCESS_RIGHTS, kGuestXxAccessRightsDefault);

    vmcs.Write(VmcsField32::GUEST_LDTR_ACCESS_RIGHTS,
               kGuestXxAccessRightsTypeW | kGuestXxAccessRightsP);

    if (vpid == kBaseProcessorVpid) {
        // Use GUEST_RIP to set the entry point on the BSP.
        vmcs.Write(VmcsFieldXX::GUEST_CS_BASE, 0);
        vmcs.Write(VmcsField16::GUEST_CS_SELECTOR, 0);
        vmcs.Write(VmcsFieldXX::GUEST_RIP, entry);
    } else {
        // Use CS to set the entry point on APs.
        vmcs.Write(VmcsFieldXX::GUEST_CS_BASE, entry);
        vmcs.Write(VmcsField16::GUEST_CS_SELECTOR, static_cast<uint16_t>(entry >> 4));
        vmcs.Write(VmcsFieldXX::GUEST_RIP, 0);
    }
    vmcs.Write(VmcsField32::GUEST_CS_LIMIT, 0xffff);
    vmcs.Write(VmcsFieldXX::GUEST_TR_BASE, 0);
    vmcs.Write(VmcsField16::GUEST_TR_SELECTOR, 0);
    vmcs.Write(VmcsField32::GUEST_TR_LIMIT, 0xffff);
    vmcs.Write(VmcsFieldXX::GUEST_DS_BASE, 0);
    vmcs.Write(VmcsField32::GUEST_DS_LIMIT, 0xffff);
    vmcs.Write(VmcsFieldXX::GUEST_SS_BASE, 0);
    vmcs.Write(VmcsField32::GUEST_SS_LIMIT, 0xffff);
    vmcs.Write(VmcsFieldXX::GUEST_ES_BASE, 0);
    vmcs.Write(VmcsField32::GUEST_ES_LIMIT, 0xffff);
    vmcs.Write(VmcsFieldXX::GUEST_FS_BASE, 0);
    vmcs.Write(VmcsField32::GUEST_FS_LIMIT, 0xffff);
    vmcs.Write(VmcsFieldXX::GUEST_GS_BASE, 0);
    vmcs.Write(VmcsField32::GUEST_GS_LIMIT, 0xffff);
    vmcs.Write(VmcsField32::GUEST_LDTR_LIMIT, 0xffff);
    vmcs.Write(VmcsFieldXX::GUEST_GDTR_BASE, 0);
    vmcs.Write(VmcsField32::GUEST_GDTR_LIMIT, 0xffff);
    vmcs.Write(VmcsFieldXX::GUEST_IDTR_BASE, 0);
    vmcs.Write(VmcsField32::GUEST_IDTR_LIMIT, 0xffff);

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
    vmcs.Write(VmcsFieldXX::GUEST_CR3, 0);

    // From Volume 3, Section 24.4.2: If the “VMCS shadowing” VM-execution
    // control is 1, the VMREAD and VMWRITE instructions access the VMCS
    // referenced by this pointer (see Section 24.10). Otherwise, software
    // should set this field to FFFFFFFF_FFFFFFFFH to avoid VM-entry
    // failures (see Section 26.3.1.5).
    vmcs.Write(VmcsField64::LINK_POINTER, kLinkPointerInvalidate);

    if (x86_feature_test(X86_FEATURE_XSAVE)) {
        // Enable x87 state in guest XCR0.
        vmx_state->guest_state.xcr0 = X86_XSAVE_STATE_BIT_X87;
    }

    return ZX_OK;
}

// static
zx_status_t Vcpu::Create(Guest* guest, zx_vaddr_t entry, fbl::unique_ptr<Vcpu>* out) {
    hypervisor::GuestPhysicalAddressSpace* gpas = guest->AddressSpace();
    if (entry >= gpas->size())
        return ZX_ERR_INVALID_ARGS;

    uint16_t vpid;
    zx_status_t status = guest->AllocVpid(&vpid);
    if (status != ZX_OK) {
        return status;
    }

    auto auto_call = fbl::MakeAutoCall([guest, vpid]() {
        guest->FreeVpid(vpid);
    });

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
    thread_t* thread = hypervisor::pin_thread(vpid);

    fbl::AllocChecker ac;
    fbl::unique_ptr<Vcpu> vcpu(new (&ac) Vcpu(guest, vpid, thread));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    timer_init(&vcpu->local_apic_state_.timer);
    status = vcpu->local_apic_state_.interrupt_tracker.Init();
    if (status != ZX_OK)
        return status;

    vcpu->pvclock_state_.is_stable =
        pvclock_is_present() ? pvclock_is_stable() : x86_feature_test(X86_FEATURE_INVAR_TSC);

    VmxInfo vmx_info;
    status = vcpu->host_msr_page_.Alloc(vmx_info, 0);
    if (status != ZX_OK)
        return status;

    status = vcpu->guest_msr_page_.Alloc(vmx_info, 0);
    if (status != ZX_OK)
        return status;

    status = vcpu->vmcs_page_.Alloc(vmx_info, 0);
    if (status != ZX_OK)
        return status;
    auto_call.cancel();

    VmxRegion* region = vcpu->vmcs_page_.VirtualAddress<VmxRegion>();
    region->revision_id = vmx_info.revision_id;
    zx_paddr_t table = gpas->aspace()->arch_aspace().arch_table_phys();
    status = vmcs_init(vcpu->vmcs_page_.PhysicalAddress(), vpid, entry, guest->MsrBitmapsAddress(),
                       table, &vcpu->vmx_state_, &vcpu->host_msr_page_, &vcpu->guest_msr_page_);
    if (status != ZX_OK)
        return status;

    *out = fbl::move(vcpu);
    return ZX_OK;
}

Vcpu::Vcpu(Guest* guest, uint16_t vpid, const thread_t* thread)
    : guest_(guest), vpid_(vpid), thread_(thread), running_(false), vmx_state_(/* zero-init */) {}

Vcpu::~Vcpu() {
    if (!vmcs_page_.IsAllocated()) {
        return;
    }
    timer_cancel(&local_apic_state_.timer);
    // The destructor may be called from a different thread, therefore we must
    // pin the current thread to the same CPU as the VCPU.
    AutoPin pin(vpid_);
    vmclear(vmcs_page_.PhysicalAddress());
    __UNUSED zx_status_t status = guest_->FreeVpid(vpid_);
    DEBUG_ASSERT(status == ZX_OK);
}

// Injects an interrupt into the guest, if there is one pending.
static zx_status_t local_apic_maybe_interrupt(AutoVmcs* vmcs, LocalApicState* local_apic_state) {
    uint32_t vector;
    zx_status_t status = local_apic_state->interrupt_tracker.Pop(&vector);
    if (status != ZX_OK) {
        return status == ZX_ERR_NOT_FOUND ? ZX_OK : status;
    }

    if (vector < X86_INT_PLATFORM_BASE || vmcs->Read(VmcsFieldXX::GUEST_RFLAGS) & X86_FLAGS_IF) {
        // If the vector is non-maskable or interrupts are enabled, we inject an interrupt.
        vmcs->IssueInterrupt(vector);
    } else {
        local_apic_state->interrupt_tracker.Track(vector);
        // If interrupts are disabled, we set VM exit on interrupt enable.
        vmcs->InterruptWindowExiting(true);
    }
    return ZX_OK;
}

zx_status_t Vcpu::Resume(zx_port_packet_t* packet) {
    if (!hypervisor::check_pinned_cpu_invariant(vpid_, thread_))
        return ZX_ERR_BAD_STATE;
    zx_status_t status;
    do {
        AutoVmcs vmcs(vmcs_page_.PhysicalAddress());
        status = local_apic_maybe_interrupt(&vmcs, &local_apic_state_);
        if (status != ZX_OK) {
            return status;
        }
        if (x86_feature_test(X86_FEATURE_XSAVE)) {
            // Save the host XCR0, and load the guest XCR0.
            vmx_state_.host_state.xcr0 = x86_xgetbv(0);
            x86_xsetbv(0, vmx_state_.guest_state.xcr0);
        }

        // Updates guest system time if the guest subscribed to updates.
        pvclock_update_system_time(&pvclock_state_, guest_->AddressSpace());

        ktrace(TAG_VCPU_ENTER, 0, 0, 0, 0);
        running_.store(true);
        status = vmx_enter(&vmx_state_);
        running_.store(false);
        if (x86_feature_test(X86_FEATURE_XSAVE)) {
            // Save the guest XCR0, and load the host XCR0.
            vmx_state_.guest_state.xcr0 = x86_xgetbv(0);
            x86_xsetbv(0, vmx_state_.host_state.xcr0);
        }

        if (status != ZX_OK) {
            ktrace_vcpu(TAG_VCPU_EXIT, VCPU_FAILURE);
            uint64_t error = vmcs.Read(VmcsField32::INSTRUCTION_ERROR);
            dprintf(INFO, "VCPU resume failed: %#lx\n", error);
        } else {
            vmx_state_.resume = true;
            status = vmexit_handler(&vmcs, &vmx_state_.guest_state, &local_apic_state_,
                                    &pvclock_state_, guest_->AddressSpace(), guest_->Traps(),
                                    packet);
        }
    } while (status == ZX_OK);
    return status == ZX_ERR_NEXT ? ZX_OK : status;
}

void vmx_exit(VmxState* vmx_state) {
    DEBUG_ASSERT(arch_ints_disabled());

    // Reload the task segment in order to restore its limit. VMX always
    // restores it with a limit of 0x67, which excludes the IO bitmap.
    seg_sel_t selector = TSS_SELECTOR(arch_curr_cpu_num());
    x86_clear_tss_busy(selector);
    x86_ltr(selector);
}

zx_status_t Vcpu::Interrupt(uint32_t vector) {
    bool signaled = false;
    zx_status_t status = local_apic_state_.interrupt_tracker.Interrupt(vector, &signaled);
    if (status != ZX_OK) {
        return status;
    } else if (!signaled && running_.load()) {
        mp_interrupt(MP_IPI_TARGET_MASK, cpu_num_to_mask(hypervisor::cpu_of(vpid_)));
    }
    return ZX_OK;
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

zx_status_t Vcpu::ReadState(uint32_t kind, void* buf, size_t len) const {
    if (!hypervisor::check_pinned_cpu_invariant(vpid_, thread_))
        return ZX_ERR_BAD_STATE;
    switch (kind) {
    case ZX_VCPU_STATE: {
        if (len != sizeof(zx_vcpu_state_t))
            break;
        auto state = static_cast<zx_vcpu_state_t*>(buf);
        register_copy(state, vmx_state_.guest_state);
        AutoVmcs vmcs(vmcs_page_.PhysicalAddress());
        state->rsp = vmcs.Read(VmcsFieldXX::GUEST_RSP);
        state->rflags = vmcs.Read(VmcsFieldXX::GUEST_RFLAGS) & X86_FLAGS_USER;
        return ZX_OK;
    }
    }
    return ZX_ERR_INVALID_ARGS;
}

zx_status_t Vcpu::WriteState(uint32_t kind, const void* buf, size_t len) {
    if (!hypervisor::check_pinned_cpu_invariant(vpid_, thread_))
        return ZX_ERR_BAD_STATE;
    switch (kind) {
    case ZX_VCPU_STATE: {
        if (len != sizeof(zx_vcpu_state_t))
            break;
        auto state = static_cast<const zx_vcpu_state_t*>(buf);
        register_copy(&vmx_state_.guest_state, *state);
        AutoVmcs vmcs(vmcs_page_.PhysicalAddress());
        vmcs.Write(VmcsFieldXX::GUEST_RSP, state->rsp);
        if (state->rflags & X86_FLAGS_RESERVED_ONES) {
            const uint64_t rflags = vmcs.Read(VmcsFieldXX::GUEST_RFLAGS);
            const uint64_t user_flags = (rflags & ~X86_FLAGS_USER) |
                                        (state->rflags & X86_FLAGS_USER);
            vmcs.Write(VmcsFieldXX::GUEST_RFLAGS, user_flags);
        }
        return ZX_OK;
    }
    case ZX_VCPU_IO: {
        if (len != sizeof(zx_vcpu_io_t))
            break;
        auto io = static_cast<const zx_vcpu_io_t*>(buf);
        memcpy(&vmx_state_.guest_state.rax, io->data, io->access_size);
        return ZX_OK;
    }
    }
    return ZX_ERR_INVALID_ARGS;
}

bool cr0_is_invalid(AutoVmcs* vmcs, uint64_t cr0_value) {
    uint64_t check_value = cr0_value;
    // From Volume 3, Section 26.3.1.1: PE and PG bits of CR0 are not checked when unrestricted
    // guest is enabled. Set both here to avoid clashing with X86_MSR_IA32_VMX_CR0_FIXED1.
    if (vmcs->Read(VmcsField32::PROCBASED_CTLS2) & kProcbasedCtls2UnrestrictedGuest) {
        check_value |= X86_CR0_PE | X86_CR0_PG;
    }
    return cr_is_invalid(check_value, X86_MSR_IA32_VMX_CR0_FIXED0, X86_MSR_IA32_VMX_CR0_FIXED1);
}
