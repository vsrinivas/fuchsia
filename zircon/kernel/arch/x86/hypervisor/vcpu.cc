// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <lib/arch/x86/boot-cpuid.h>
#include <lib/arch/x86/speculation.h>
#include <lib/boot-options/boot-options.h>
#include <lib/fit/defer.h>
#include <lib/ktrace.h>
#include <zircon/syscalls/hypervisor.h>

#include <new>

#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/x86/hypervisor/invalidate.h>
#include <arch/x86/platform_access.h>
#include <arch/x86/pv.h>
#include <hwreg/x86msr.h>
#include <hypervisor/cpu.h>
#include <hypervisor/ktrace.h>
#include <kernel/percpu.h>
#include <kernel/stats.h>
#include <vm/fault.h>
#include <vm/pmm.h>
#include <vm/vm_object.h>

#include "pv_priv.h"
#include "vcpu_priv.h"
#include "vmexit_priv.h"
#include "vmx_cpu_state_priv.h"

namespace {

constexpr uint32_t kInterruptInfoValid = 1u << 31;
constexpr uint32_t kInterruptInfoDeliverErrorCode = 1u << 11;
constexpr uint32_t kInterruptTypeNmi = 2u << 8;
constexpr uint32_t kInterruptTypeHardwareException = 3u << 8;
constexpr uint32_t kInterruptTypeSoftwareException = 6u << 8;
constexpr uint16_t kBaseProcessorVpid = 1;

void vmptrld(paddr_t pa) {
  uint8_t err;

  __asm__ __volatile__("vmptrld %[pa]"
                       : "=@ccna"(err)  // Set `err` on error (C or Z flag set)
                       : [pa] "m"(pa)
                       : "cc", "memory");

  ASSERT(!err);
}

void vmclear(paddr_t pa) {
  uint8_t err;

  __asm__ __volatile__("vmclear %[pa]"
                       : "=@ccna"(err)  // Set `err` on error (C or Z flag set)
                       : [pa] "m"(pa)
                       : "cc", "memory");

  ASSERT(!err);
}

uint64_t vmread(uint64_t field) {
  uint8_t err;
  uint64_t val;

  __asm__ __volatile__("vmread %[field], %[val]"
                       : [val] "=r"(val),
                         "=@ccna"(err)  // Set `err` on error (C or Z flag set)
                       : [field] "r"(field)
                       : "cc");
  ASSERT(!err);
  return val;
}

void vmwrite(uint64_t field, uint64_t val) {
  uint8_t err;

  __asm__ __volatile__("vmwrite %[val], %[field]"
                       : "=@ccna"(err)  // Set `err` on error (C or Z flag set)
                       : [val] "r"(val), [field] "r"(field)
                       : "cc");
  ASSERT(!err);
}

bool has_error_code(uint32_t vector) {
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

struct MsrListEntry {
  uint32_t msr;
  uint32_t reserved;
  uint64_t value;
} __PACKED;

void edit_msr_list(VmxPage* msr_list_page, size_t index, uint32_t msr, uint64_t value) {
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

void swap_extended_registers(uint8_t* save_extended_registers, uint64_t& save_xcr0, bool save,
                             uint8_t* load_extended_registers, uint64_t& load_xcr0, bool load) {
  x86_extended_register_save_state(save_extended_registers);
  if (save) {
    save_xcr0 = x86_xgetbv(0);
  }
  if (load) {
    x86_xsetbv(0, load_xcr0);
  }
  x86_extended_register_restore_state(load_extended_registers);
}

template <typename Out, typename In>
void register_copy(Out& out, const In& in) {
  out.rax = in.rax;
  out.rcx = in.rcx;
  out.rdx = in.rdx;
  out.rbx = in.rbx;
  out.rbp = in.rbp;
  out.rsi = in.rsi;
  out.rdi = in.rdi;
  out.r8 = in.r8;
  out.r9 = in.r9;
  out.r10 = in.r10;
  out.r11 = in.r11;
  out.r12 = in.r12;
  out.r13 = in.r13;
  out.r14 = in.r14;
  out.r15 = in.r15;
}

zx_status_t vmcs_init(AutoVmcs& vmcs, const VcpuConfig& config, uint16_t vpid, uintptr_t entry,
                      paddr_t msr_bitmaps_address, paddr_t ept_pml4, VmxState* vmx_state,
                      VmxPage* host_msr_page, VmxPage* guest_msr_page,
                      uint8_t* extended_register_state) {
  // Setup secondary processor-based VMCS controls.
  zx_status_t status =
      vmcs.SetControl(VmcsField32::PROCBASED_CTLS2, read_msr(X86_MSR_IA32_VMX_PROCBASED_CTLS2), 0,
                      // Enable use of extended page tables.
                      kProcbasedCtls2Ept |
                          // Enable use of RDTSCP instruction.
                          kProcbasedCtls2Rdtscp |
                          // Enable X2APIC.
                          kProcbasedCtls2x2Apic |
                          // Associate cached translations of linear
                          // addresses with a virtual processor ID.
                          kProcbasedCtls2Vpid |
                          // If `unrestricted`, enable unrestricted guest.
                          (config.unrestricted ? kProcbasedCtls2UnrestrictedGuest : 0),
                      // If not `unrestricted`, disable unrestricted guest.
                      (config.unrestricted ? 0 : kProcbasedCtls2UnrestrictedGuest));
  if (status != ZX_OK) {
    return status;
  }

  // Enable use of INVPCID instruction if available.
  vmcs.SetControl(VmcsField32::PROCBASED_CTLS2, read_msr(X86_MSR_IA32_VMX_PROCBASED_CTLS2),
                  vmcs.Read(VmcsField32::PROCBASED_CTLS2), kProcbasedCtls2Invpcid, 0);

  // Setup pin-based VMCS controls.
  status =
      vmcs.SetControl(VmcsField32::PINBASED_CTLS, read_msr(X86_MSR_IA32_VMX_TRUE_PINBASED_CTLS),
                      read_msr(X86_MSR_IA32_VMX_PINBASED_CTLS),
                      // External interrupts cause a VM exit.
                      kPinbasedCtlsExtIntExiting |
                          // Non-maskable interrupts cause a VM exit.
                          kPinbasedCtlsNmiExiting,
                      0);
  if (status != ZX_OK) {
    return status;
  }

  const uint32_t cr_ctls =
      // VM exit on CR3 load.
      kProcbasedCtlsCr3LoadExiting |
      // VM exit on CR3 store.
      kProcbasedCtlsCr3StoreExiting |
      // VM exit on CR8 load.
      kProcbasedCtlsCr8LoadExiting |
      // VM exit on CR8 store.
      kProcbasedCtlsCr8StoreExiting;
  // Setup primary processor-based VMCS controls.
  status =
      vmcs.SetControl(VmcsField32::PROCBASED_CTLS, read_msr(X86_MSR_IA32_VMX_TRUE_PROCBASED_CTLS),
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
                          // Enable secondary processor-based controls.
                          kProcbasedCtlsProcbasedCtls2 |
                          // If `cr_exiting`, enable VM exit on CRs.
                          (config.cr_exiting ? cr_ctls : 0),
                      // If not `cr_exiting`, disable VM exit on CRs.
                      (config.cr_exiting ? 0 : cr_ctls));
  if (status != ZX_OK) {
    return status;
  }

  // We only enable interrupt-window exiting above to ensure that the
  // processor supports it for later use. So disable it for now.
  vmcs.InterruptWindowExiting(false);

  // Setup VM-exit VMCS controls.
  status = vmcs.SetControl(VmcsField32::EXIT_CTLS, read_msr(X86_MSR_IA32_VMX_TRUE_EXIT_CTLS),
                           read_msr(X86_MSR_IA32_VMX_EXIT_CTLS),
                           // Logical processor is in 64-bit mode after VM
                           // exit. On VM exit CS.L, IA32_EFER.LME, and
                           // IA32_EFER.LMA is set to true.
                           kExitCtls64bitMode |
                               // Acknowledge external interrupt on exit.
                               kExitCtlsAckIntOnExit |
                               // Save the guest IA32_PAT MSR on exit.
                               kExitCtlsSaveIa32Pat |
                               // Load the host IA32_PAT MSR on exit.
                               kExitCtlsLoadIa32Pat |
                               // Save the guest IA32_EFER MSR on exit.
                               kExitCtlsSaveIa32Efer |
                               // Load the host IA32_EFER MSR on exit.
                               kExitCtlsLoadIa32Efer,
                           0);
  if (status != ZX_OK) {
    return status;
  }

  // Whether we are configuring the base processor. The base processor starts in
  // 64-bit mode with all features enabled. For secondary processors, they must
  // be bootstrapped by the operating system.
  //
  // If there is no base processor for this VCPU type, then default to true.
  // This is important for direct mode, as all VCPUs will be treated as base
  // processors.
  const bool is_base_processor = config.has_base_processor ? vpid == kBaseProcessorVpid : true;

  // Setup VM-entry VMCS controls.
  // Load the guest IA32_PAT MSR and IA32_EFER MSR on entry.
  uint32_t entry_ctls = kEntryCtlsLoadIa32Pat | kEntryCtlsLoadIa32Efer;
  if (is_base_processor) {
    // On the BSP, go straight to 64-bit mode on entry.
    entry_ctls |= kEntryCtls64bitMode;
  }
  status = vmcs.SetControl(VmcsField32::ENTRY_CTLS, read_msr(X86_MSR_IA32_VMX_TRUE_ENTRY_CTLS),
                           read_msr(X86_MSR_IA32_VMX_ENTRY_CTLS), entry_ctls, 0);
  if (status != ZX_OK) {
    return status;
  }

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
  invvpid(InvVpid::SINGLE_CONTEXT, vpid, 0);

  // From Volume 3, Section 28.2: The extended page-table mechanism (EPT) is a
  // feature that can be used to support the virtualization of physical
  // memory. When EPT is in use, certain addresses that would normally be
  // treated as physical addresses (and used to access memory) are instead
  // treated as guest-physical addresses. Guest-physical addresses are
  // translated by traversing a set of EPT paging structures to produce
  // physical addresses that are used to access memory.
  const auto eptp = ept_pointer_from_pml4(ept_pml4);
  vmcs.Write(VmcsField64::EPT_POINTER, eptp);

  // Setup MSR handling.
  vmcs.Write(VmcsField64::MSR_BITMAPS_ADDRESS, msr_bitmaps_address);

  edit_msr_list(host_msr_page, 0, X86_MSR_IA32_KERNEL_GS_BASE,
                read_msr(X86_MSR_IA32_KERNEL_GS_BASE));
  edit_msr_list(host_msr_page, 1, X86_MSR_IA32_STAR, read_msr(X86_MSR_IA32_STAR));
  edit_msr_list(host_msr_page, 2, X86_MSR_IA32_LSTAR, read_msr(X86_MSR_IA32_LSTAR));
  edit_msr_list(host_msr_page, 3, X86_MSR_IA32_FMASK, read_msr(X86_MSR_IA32_FMASK));
  edit_msr_list(host_msr_page, 4, X86_MSR_IA32_TSC_AUX, read_msr(X86_MSR_IA32_TSC_AUX));
  vmcs.Write(VmcsField64::EXIT_MSR_LOAD_ADDRESS, host_msr_page->PhysicalAddress());
  vmcs.Write(VmcsField32::EXIT_MSR_LOAD_COUNT, 5);

  edit_msr_list(guest_msr_page, 0, X86_MSR_IA32_KERNEL_GS_BASE, 0);
  edit_msr_list(guest_msr_page, 1, X86_MSR_IA32_STAR, 0);
  edit_msr_list(guest_msr_page, 2, X86_MSR_IA32_LSTAR, 0);
  edit_msr_list(guest_msr_page, 3, X86_MSR_IA32_FMASK, 0);
  edit_msr_list(guest_msr_page, 4, X86_MSR_IA32_TSC_AUX, 0);
  vmcs.Write(VmcsField64::EXIT_MSR_STORE_ADDRESS, guest_msr_page->PhysicalAddress());
  vmcs.Write(VmcsField32::EXIT_MSR_STORE_COUNT, 5);
  vmcs.Write(VmcsField64::ENTRY_MSR_LOAD_ADDRESS, guest_msr_page->PhysicalAddress());
  vmcs.Write(VmcsField32::ENTRY_MSR_LOAD_COUNT, 5);

  // Setup VMCS host state.
  //
  // NOTE: We are pinned to a thread when executing this function, therefore
  // it is acceptable to use per-CPU state.
  x86_percpu* percpu = x86_get_percpu();
  vmcs.Write(VmcsField32::HOST_IA32_SYSENTER_CS, 0);
  vmcs.Write(VmcsFieldXX::HOST_IA32_SYSENTER_ESP, 0);
  vmcs.Write(VmcsFieldXX::HOST_IA32_SYSENTER_EIP, 0);
  vmcs.Write(VmcsField64::HOST_IA32_PAT, read_msr(X86_MSR_IA32_PAT));
  vmcs.Write(VmcsField64::HOST_IA32_EFER, read_msr(X86_MSR_IA32_EFER));
  vmcs.Write(VmcsFieldXX::HOST_CR0, x86_get_cr0());
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
  vmcs.Write(VmcsFieldXX::HOST_RSP, reinterpret_cast<uint64_t>(vmx_state));
  vmcs.Write(VmcsFieldXX::HOST_RIP, reinterpret_cast<uint64_t>(vmx_exit_asm));

  // Setup VMCS guest state.
  uint64_t cr0 = X86_CR0_ET |  // Enable extension type
                 X86_CR0_NE |  // Enable internal x87 exception handling
                 X86_CR0_WP;   // Enable supervisor write protect
  if (is_base_processor) {
    // Enable protected mode and paging on the primary VCPU.
    cr0 |= X86_CR0_PE |  // Enable protected mode
           X86_CR0_PG;   // Enable paging
  }
  if (cr0_is_invalid(vmcs, cr0)) {
    return ZX_ERR_BAD_STATE;
  }
  vmcs.Write(VmcsFieldXX::GUEST_CR0, cr0);

  // Enable FXSAVE, VMX, and XSAVE.
  uint64_t cr4 = X86_CR4_OSFXSR | X86_CR4_VMXE | X86_CR4_OSXSAVE;
  if (is_base_processor) {
    // Enable PAE and PGE on the BSP.
    cr4 |= X86_CR4_PAE | X86_CR4_PGE;
  }
  if (cr_is_invalid(cr4, X86_MSR_IA32_VMX_CR4_FIXED0, X86_MSR_IA32_VMX_CR4_FIXED1)) {
    return ZX_ERR_BAD_STATE;
  }
  vmcs.Write(VmcsFieldXX::GUEST_CR4, cr4);

  vmcs.Write(VmcsField64::GUEST_IA32_PAT, read_msr(X86_MSR_IA32_PAT));

  uint64_t guest_efer = read_msr(X86_MSR_IA32_EFER);
  if (!is_base_processor) {
    // Disable LME and LMA on all but the BSP.
    guest_efer &= ~(X86_EFER_LME | X86_EFER_LMA);
  }
  vmcs.Write(VmcsField64::GUEST_IA32_EFER, guest_efer);

  uint32_t cs_access_rights =
      kGuestXxAccessRightsDefault | kGuestXxAccessRightsTypeE | kGuestXxAccessRightsTypeCode;
  if (is_base_processor) {
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

  if (is_base_processor) {
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

  // From Volume 3, Section 24.4.2: If the “VMCS shadowing” VM-execution
  // control is 1, the VMREAD and VMWRITE instructions access the VMCS
  // referenced by this pointer (see Section 24.10). Otherwise, software
  // should set this field to FFFFFFFF_FFFFFFFFH to avoid VM-entry
  // failures (see Section 26.3.1.5).
  vmcs.Write(VmcsField64::LINK_POINTER, kLinkPointerInvalidate);

  if (x86_xsave_supported()) {
    // Set initial guest XCR0 to host XCR0.
    vmx_state->host_state.xcr0 = x86_xgetbv(0);
    vmx_state->guest_state.xcr0 =
        X86_XSAVE_STATE_BIT_X87 | X86_XSAVE_STATE_BIT_SSE | X86_XSAVE_STATE_BIT_AVX;
    x86_extended_register_init_state_from_bv(extended_register_state, vmx_state->guest_state.xcr0);
  }

  return ZX_OK;
}

// Injects an interrupt into the guest, if there is one pending.
zx_status_t local_apic_maybe_interrupt(AutoVmcs* vmcs, LocalApicState* local_apic_state) {
  // Since hardware generated exceptions are delivered to the guest directly,
  // the only exceptions we see here are those we generate in the VMM, e.g. GP
  // faults in vmexit handlers. Therefore we simplify interrupt priority to 1)
  // NMIs, 2) interrupts, and 3) generated exceptions. See Volume 3, Section
  // 6.9, Table 6-2.
  uint32_t vector = X86_INT_COUNT;
  bool pending = local_apic_state->interrupt_tracker.TryPop(X86_INT_NMI);
  if (pending) {
    vector = X86_INT_NMI;
  } else {
    // Pop scans vectors from highest to lowest, which will correctly pop
    // interrupts before exceptions. All vectors <= X86_INT_VIRT except the NMI
    // vector are exceptions.
    pending = local_apic_state->interrupt_tracker.Pop(&vector);
    if (!pending) {
      return ZX_OK;
    }
    // If type isn't inactive, then Pop should have initialized vector to a
    // valid value.
    DEBUG_ASSERT(vector != X86_INT_COUNT);
  }

  // NMI injection is blocked if an NMI is already being serviced (Volume 3,
  // Section 24.4.2, Table 24-3), and mov ss blocks *all* interrupts (Volume 2
  // Section 4.3 MOV-Move instruction). Note that the IF flag does not affect
  // NMIs (Volume 3, Section 6.8.1).
  auto can_inject_nmi = [vmcs] {
    return (vmcs->Read(VmcsField32::GUEST_INTERRUPTIBILITY_STATE) &
            (kInterruptibilityNmiBlocking | kInterruptibilityMovSsBlocking)) == 0;
  };
  // External interrupts can be blocked due to STI, move SS or the IF flag.
  auto can_inject_external_int = [vmcs] {
    return (vmcs->Read(VmcsFieldXX::GUEST_RFLAGS) & X86_FLAGS_IF) &&
           (vmcs->Read(VmcsField32::GUEST_INTERRUPTIBILITY_STATE) &
            (kInterruptibilityStiBlocking | kInterruptibilityMovSsBlocking)) == 0;
  };

  if (vector > X86_INT_VIRT && vector < X86_INT_PLATFORM_BASE) {
    dprintf(INFO, "Invalid interrupt vector: %u\n", vector);
    return ZX_ERR_NOT_SUPPORTED;
  } else if ((vector >= X86_INT_PLATFORM_BASE && !can_inject_external_int()) ||
             (vector == X86_INT_NMI && !can_inject_nmi())) {
    local_apic_state->interrupt_tracker.Track(vector);
    // If interrupts are disabled, we set VM exit on interrupt enable.
    vmcs->InterruptWindowExiting(true);
    return ZX_OK;
  }

  // If the vector is non-maskable or interrupts are enabled, inject interrupt.
  vmcs->IssueInterrupt(vector);

  // Volume 3, Section 6.9: Lower priority exceptions are discarded; lower
  // priority interrupts are held pending. Discarded exceptions are re-generated
  // when the interrupt handler returns execution to the point in the program or
  // task where the exceptions and/or interrupts occurred.
  local_apic_state->interrupt_tracker.Clear(0, X86_INT_NMI);
  local_apic_state->interrupt_tracker.Clear(X86_INT_NMI + 1, X86_INT_VIRT + 1);

  return ZX_OK;
}

void interrupt_cpu(Thread* thread, cpu_num_t last_cpu) TA_REQ(ThreadLock::Get()) {
  // Check if the VCPU is running and whether to send an IPI. We hold the thread
  // lock to guard against thread migration between CPUs during the check.
  //
  // NOTE: `last_cpu` may be currently set to `INVALID_CPU` due to thread
  // migration between CPUs.
  if (thread != nullptr && thread->state() == THREAD_RUNNING && last_cpu != INVALID_CPU) {
    mp_interrupt(MP_IPI_TARGET_MASK, cpu_num_to_mask(last_cpu));
  }
}

}  // namespace

AutoVmcs::AutoVmcs(paddr_t vmcs_address, bool clear) : vmcs_address_(vmcs_address) {
  DEBUG_ASSERT(!arch_ints_disabled());
  int_state_ = arch_interrupt_save();
  arch_set_blocking_disallowed(true);
  if (clear) {
    vmclear(vmcs_address_);
  }
  vmptrld(vmcs_address_);
}

AutoVmcs::~AutoVmcs() {
  DEBUG_ASSERT(arch_ints_disabled());
  if (vmcs_address_ != 0) {
    arch_set_blocking_disallowed(false);
  }
  arch_interrupt_restore(int_state_);
}

void AutoVmcs::Invalidate() {
  if (vmcs_address_ != 0) {
    vmcs_address_ = 0;
    arch_set_blocking_disallowed(false);
  }
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

void AutoVmcs::IssueInterrupt(uint32_t vector) {
  DEBUG_ASSERT(vmcs_address_ != 0);
  uint32_t interrupt_info = kInterruptInfoValid | (vector & UINT8_MAX);
  if (vector == X86_INT_BREAKPOINT || vector == X86_INT_OVERFLOW) {
    // From Volume 3, Section 24.8.3. A VMM should use type hardware exception for all
    // exceptions other than breakpoints and overflows, which should be software exceptions.
    interrupt_info |= kInterruptTypeSoftwareException;
  } else if (vector == X86_INT_NMI) {
    interrupt_info |= kInterruptTypeNmi;
  } else if (vector <= X86_INT_VIRT) {
    // From Volume 3, Section 6.15. All other vectors from 0 to X86_INT_VIRT are exceptions.
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
    dprintf(INFO, "Failed to set VMCS controls %#x, %#x != %#x\n", static_cast<uint>(controls),
            allowed_1, set);
    return ZX_ERR_NOT_SUPPORTED;
  }
  if ((~allowed_0 & clear) != clear) {
    dprintf(INFO, "Failed to clear VMCS controls %#x, %#x != %#x\n", static_cast<uint>(controls),
            ~allowed_0, clear);
    return ZX_ERR_NOT_SUPPORTED;
  }
  if ((set & clear) != 0) {
    dprintf(INFO, "Attempted to set and clear the same VMCS controls %#x\n",
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

bool cr0_is_invalid(AutoVmcs& vmcs, uint64_t cr0_value) {
  uint64_t check_value = cr0_value;
  // From Volume 3, Section 26.3.1.1: PE and PG bits of CR0 are not checked when unrestricted
  // guest is enabled. Set both here to avoid clashing with X86_MSR_IA32_VMX_CR0_FIXED1.
  if (vmcs.Read(VmcsField32::PROCBASED_CTLS2) & kProcbasedCtls2UnrestrictedGuest) {
    check_value |= X86_CR0_PE | X86_CR0_PG;
  }
  return cr_is_invalid(check_value, X86_MSR_IA32_VMX_CR0_FIXED0, X86_MSR_IA32_VMX_CR0_FIXED1);
}

// static
template <typename V, typename G>
zx::status<ktl::unique_ptr<V>> Vcpu::Create(G& guest, uint16_t vpid, zx_vaddr_t entry) {
  if (fbl::RefPtr<VmAddressRegion> root_vmar = guest.RootVmar();
      entry < root_vmar->base() || entry >= root_vmar->base() + root_vmar->size()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  Thread* thread = Thread::Current::Get();
  if (thread->vcpu()) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  fbl::AllocChecker ac;
  ktl::unique_ptr<V> vcpu(new (&ac) V(guest, vpid, thread));
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  VmxInfo vmx_info;
  zx_status_t status = vcpu->host_msr_page_.Alloc(vmx_info, 0);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  status = vcpu->guest_msr_page_.Alloc(vmx_info, 0);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  status = vcpu->vmcs_page_.Alloc(vmx_info, 0);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  VmxRegion* region = vcpu->vmcs_page_.template VirtualAddress<VmxRegion>();
  region->revision_id = vmx_info.revision_id;

  zx_paddr_t ept_pml4 = guest.AddressSpace().arch_aspace().arch_table_phys();
  zx_paddr_t vmcs_address = vcpu->vmcs_page_.PhysicalAddress();
  // We create the `AutoVmcs` object here, so that we ensure that interrupts are
  // disabled from `vmcs_init` until `SetMigrateFn`. This is important to ensure
  // that we do not migrate CPUs while setting up the VCPU.
  AutoVmcs vmcs(vmcs_address, /*clear=*/true);
  status = vmcs_init(vmcs, V::kConfig, vpid, entry, guest.MsrBitmapsAddress(), ept_pml4,
                     &vcpu->vmx_state_, &vcpu->host_msr_page_, &vcpu->guest_msr_page_,
                     vcpu->extended_register_state_);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  // Only set the thread migrate function after we have initialised the VMCS.
  // Otherwise, the migrate function may interact with an uninitialised VMCS.
  //
  // We have to disable thread safety analysis because it's not smart enough to
  // realize that SetMigrateFn will always be called with the ThreadLock.
  thread->SetMigrateFn([vcpu = vcpu.get()](Thread* thread, auto stage)
                           TA_NO_THREAD_SAFETY_ANALYSIS { vcpu->MigrateCpu(thread, stage); });

  return zx::ok(ktl::move(vcpu));
}

Vcpu::Vcpu(Guest& guest, uint16_t vpid, Thread* thread)
    : guest_(guest),
      vpid_(vpid),
      last_cpu_(thread->LastCpu()),
      thread_(thread),
      vmx_state_(/* zero-init */) {
  thread->set_vcpu(true);
}

Vcpu::~Vcpu() {
  cpu_num_t cpu;
  {
    // Taking the ThreadLock guarantees that thread_ isn't going to be freed
    // while we access it.
    Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
    Thread* thread = thread_.load();
    if (thread != nullptr) {
      thread->set_vcpu(false);
      // Clear the migration function, so that |thread_| does not reference
      // |this| after destruction of the VCPU.
      thread->SetMigrateFnLocked(nullptr);
    }
    cpu = last_cpu_;
  }

  if (vmcs_page_.IsAllocated() && cpu != INVALID_CPU) {
    // Clear VMCS state from the CPU.
    //
    // The destructor may be called from a different thread, therefore we must
    // IPI the CPU that last run the thread.
    paddr_t paddr = vmcs_page_.PhysicalAddress();
    mp_sync_exec(
        MP_IPI_TARGET_MASK, cpu_num_to_mask(cpu),
        [](void* paddr) { vmclear(reinterpret_cast<paddr_t>(paddr)); },
        reinterpret_cast<void*>(paddr));
  }
}

void Vcpu::MigrateCpu(Thread* thread, Thread::MigrateStage stage) {
  // Volume 3, Section 31.8.2: An MP-aware VMM is free to assign any logical
  // processor to a VM. But for performance considerations, moving a guest VMCS
  // to another logical processor is slower than resuming that guest VMCS on the
  // same logical processor. Certain VMX performance features (such as caching
  // of portions of the VMCS in the processor) are optimized for a guest VMCS
  // that runs on the same logical processor.
  //
  // If the VMCS regions are identical (same revision ID) the following sequence
  // can be used to move or copy the VMCS from one logical processor to another:
  switch (stage) {
    // * Perform a VMCLEAR operation on the source logical processor. This
    //   ensures that all VMCS data that may be cached by the processor are
    //   flushed to memory.
    case Thread::MigrateStage::Before: {
      vmclear(vmcs_page_.PhysicalAddress());
      // After VMCLEAR, `last_cpu_` can be cleared to indicate this VCPU is both
      // not presently running, and its state is not loaded anywhere.
      last_cpu_ = INVALID_CPU;
      break;
    }
    // * Copy the VMCS region from one memory location to another location. This
    //   is an optional step assuming the VMM wishes to relocate the VMCS or
    //   move the VMCS to another system.
    // * Perform a VMPTRLD of the physical address of VMCS region on the
    //   destination processor to establish its current VMCS pointer.
    case Thread::MigrateStage::After: {
      // Volume 3, Section 31.8.2: To migrate a VMCS to another logical
      // processor, a VMM must use the sequence of VMCLEAR, VMPTRLD and
      // VMLAUNCH.
      //
      // We set `resume` to false so that `vmx_enter` will call VMLAUNCH when
      // entering the the guest, instead of VMRESUME.
      vmx_state_.resume = false;

      // Before performing the VMPTRLD, update the `last_cpu_` for
      // `Vcpu::Interrupt()` and `vmcs_page_` state tracking. It is assumed that
      // the `Thread::MigrateStage::Before` stage already happened and that a
      // VMCLEAR has been performed on `last_cpu_`, hence the previous value of
      DEBUG_ASSERT(last_cpu_ == INVALID_CPU);
      last_cpu_ = thread->LastCpuLocked();

      // Load the VMCS on the destination processor.
      vmptrld(vmcs_page_.PhysicalAddress());

      // Update the host MSR list entries with the per-CPU variables of the
      // destination processor.
      edit_msr_list(&host_msr_page_, 5, X86_MSR_IA32_TSC_AUX, read_msr(X86_MSR_IA32_TSC_AUX));

      // Update the VMCS with the per-CPU variables of the destination
      // processor.
      x86_percpu* percpu = x86_get_percpu();
      vmwrite(static_cast<uint64_t>(VmcsField16::HOST_TR_SELECTOR), TSS_SELECTOR(percpu->cpu_num));
      vmwrite(static_cast<uint64_t>(VmcsFieldXX::HOST_FS_BASE), thread->arch().fs_base);
      vmwrite(static_cast<uint64_t>(VmcsFieldXX::HOST_GS_BASE), read_msr(X86_MSR_IA32_GS_BASE));
      vmwrite(static_cast<uint64_t>(VmcsFieldXX::HOST_TR_BASE),
              reinterpret_cast<uint64_t>(&percpu->default_tss));

      // Invalidate TLB mappings for the VPID.
      invvpid(InvVpid::SINGLE_CONTEXT, vpid_, 0);
      break;
    }
    case Thread::MigrateStage::Exiting: {
      // The `thread_` is exiting and so we must clear our reference to it.
      thread_.store(nullptr);
      break;
    }
  }
}

void Vcpu::LoadExtendedRegisters(AutoVmcs& vmcs) {
  arch_thread& thread = Thread::Current::Get()->arch();
  bool save_host = x86_xsave_supported();
  bool load_guest = vmcs.Read(VmcsFieldXX::GUEST_CR4) & X86_CR4_OSXSAVE;
  swap_extended_registers(thread.extended_register_buffer, vmx_state_.host_state.xcr0, save_host,
                          extended_register_state_, vmx_state_.guest_state.xcr0, load_guest);
}

void Vcpu::SaveExtendedRegisters(AutoVmcs& vmcs) {
  arch_thread& thread = Thread::Current::Get()->arch();
  bool save_guest = vmcs.Read(VmcsFieldXX::GUEST_CR4) & X86_CR4_OSXSAVE;
  bool load_host = x86_xsave_supported();
  swap_extended_registers(extended_register_state_, vmx_state_.guest_state.xcr0, save_guest,
                          thread.extended_register_buffer, vmx_state_.host_state.xcr0, load_host);
}

zx_status_t vmx_enter(VmxState* vmx_state) {
  // Perform the low-level vmlaunch or vmresume, entering the guest,
  // and returning when the guest exits.
  zx_status_t status = vmx_enter_asm(vmx_state);

  DEBUG_ASSERT(arch_ints_disabled());

  // Reload the task segment in order to restore its limit. VMX always
  // restores it with a limit of 0x67, which excludes the IO bitmap.
  seg_sel_t selector = TSS_SELECTOR(arch_curr_cpu_num());
  x86_clear_tss_busy(selector);
  x86_ltr(selector);

  return status;
}

template <typename PreEnterFn, typename PostExitFn>
zx_status_t Vcpu::EnterInternal(PreEnterFn pre_enter, PostExitFn post_exit,
                                zx_port_packet_t& packet) {
  Thread* current_thread = Thread::Current::Get();
  if (current_thread != thread_) {
    return ZX_ERR_BAD_STATE;
  }

  bool extended_registers_loaded = false;
  auto defer = fit::defer([this, &extended_registers_loaded] {
    if (extended_registers_loaded) {
      AutoVmcs vmcs(vmcs_page_.PhysicalAddress());
      SaveExtendedRegisters(vmcs);
    }
    // Spectre V2: Ensure that code executed in the VM guest cannot influence
    // indirect branch prediction in the host.
    //
    // TODO(fxbug.dev/33667): We may be able to avoid the IBPB here; the kernel
    // is either built with a retpoline or has Enhanced IBRS enabled. We
    // currently execute an IBPB on context-switch to a new aspace. The IBPB is
    // currently only here to protect hypervisor user threads.
    if (!gBootOptions->x86_disable_spec_mitigations && x86_cpu_has_ibpb()) {
      arch::IssueIbpb(arch::BootCpuidIo{}, hwreg::X86MsrIo{});
    }
  });

  zx_status_t status;
  do {
    // If the thread was killed or suspended, then we should exit with an error.
    status = current_thread->CheckKillOrSuspendSignal();
    if (status != ZX_OK) {
      return status;
    }
    AutoVmcs vmcs(vmcs_page_.PhysicalAddress());

    // We check whether a kick was requested before entering the guest so that:
    // 1. When we enter the syscall, we can return immediately without entering
    //    the guest.
    // 2. If we have already exited the guest to handle a packet, it allows us
    //    to return and gives user-space a chance to handle that packet, without
    //    the request to kick interfering with the packet in-flight.
    //
    // We also do this after we have disabled interrupts, so if an interrupt was
    // fired before we disabled interrupts, we have the opportunity to check
    // whether a kick was requested, but the interrupt was lost. If an interrupt
    // is fired after we have disabled interrupts, when we enter the guest we
    // will exit due to the interrupt, and run this check again.
    if (kicked_.exchange(false)) {
      return ZX_ERR_CANCELED;
    }

    status = pre_enter(vmcs);
    if (status != ZX_OK) {
      return status;
    }

    if (!extended_registers_loaded) {
      LoadExtendedRegisters(vmcs);
      extended_registers_loaded = true;
    }

    if (x86_cpu_should_l1d_flush_on_vmentry()) {
      // L1TF: Flush L1D$ before entering vCPU. If the CPU is affected by MDS,
      // also flush microarchitectural buffers.
      write_msr(X86_MSR_IA32_FLUSH_CMD, 1);
    } else if (x86_cpu_should_md_clear_on_user_return()) {
      // MDS: If the processor is not affected by L1TF but is affected by MDS or
      // TAA, flush microarchitectural buffers.
      mds_buff_overwrite();
    }

    ktrace(TAG_VCPU_ENTER, 0, 0, 0, 0);
    GUEST_STATS_INC(vm_entries);
    status = vmx_enter(&vmx_state_);
    GUEST_STATS_INC(vm_exits);

    if (!gBootOptions->x86_disable_spec_mitigations) {
      // Spectre V2: Ensure that code executed in the VM guest cannot influence
      // return address prediction in the host.
      x86_ras_fill();
    }

    if (status != ZX_OK) {
      ktrace_vcpu_exit(VCPU_FAILURE, vmcs.Read(VmcsFieldXX::GUEST_RIP));
      uint64_t error = vmcs.Read(VmcsField32::INSTRUCTION_ERROR);
      dprintf(INFO, "VCPU enter failed: Instruction error %lu\n", error);
    } else {
      vmx_state_.resume = true;
      status = post_exit(vmcs, packet);
    }
  } while (status == ZX_OK);
  return status == ZX_ERR_NEXT ? ZX_OK : status;
}

zx_status_t Vcpu::ReadState(zx_vcpu_state_t& vcpu_state) {
  if (Thread::Current::Get() != thread_) {
    return ZX_ERR_BAD_STATE;
  }
  register_copy(vcpu_state, vmx_state_.guest_state);
  AutoVmcs vmcs(vmcs_page_.PhysicalAddress());
  vcpu_state.rsp = vmcs.Read(VmcsFieldXX::GUEST_RSP);
  vcpu_state.rflags = vmcs.Read(VmcsFieldXX::GUEST_RFLAGS) & X86_FLAGS_USER;
  return ZX_OK;
}

zx_status_t Vcpu::WriteState(const zx_vcpu_state_t& vcpu_state) {
  if (Thread::Current::Get() != thread_) {
    return ZX_ERR_BAD_STATE;
  }
  register_copy(vmx_state_.guest_state, vcpu_state);
  AutoVmcs vmcs(vmcs_page_.PhysicalAddress());
  vmcs.Write(VmcsFieldXX::GUEST_RSP, vcpu_state.rsp);
  if (vcpu_state.rflags & X86_FLAGS_RESERVED_ONES) {
    const uint64_t rflags = vmcs.Read(VmcsFieldXX::GUEST_RFLAGS);
    const uint64_t user_flags = (rflags & ~X86_FLAGS_USER) | (vcpu_state.rflags & X86_FLAGS_USER);
    vmcs.Write(VmcsFieldXX::GUEST_RFLAGS, user_flags);
  }
  return ZX_OK;
}

void Vcpu::GetInfo(zx_info_vcpu_t* info) {
  if (kicked_.load()) {
    info->flags |= ZX_INFO_VCPU_FLAG_KICKED;
  }
}

// static
zx::status<ktl::unique_ptr<Vcpu>> NormalVcpu::Create(NormalGuest& guest, zx_vaddr_t entry) {
  auto vpid = guest.TryAllocVpid();
  if (vpid.is_error()) {
    return vpid.take_error();
  }
  auto vcpu = Vcpu::Create<NormalVcpu>(guest, *vpid, entry);
  if (vcpu.is_error()) {
    auto result = guest.FreeVpid(*vpid);
    ASSERT(result.is_ok());
    return vcpu.take_error();
  }
  // Setup PV clock state.
  vcpu->pv_clock_state_.is_stable = x86_hypervisor_has_pv_clock()
                                        ? pv_clock_is_stable()
                                        : x86_feature_test(X86_FEATURE_INVAR_TSC);
  AutoVmcs vmcs(vcpu->vmcs_page_.PhysicalAddress());
  // Enable use of PAUSE-loop exiting if available.
  zx_status_t status =
      vmcs.SetControl(VmcsField32::PROCBASED_CTLS2, read_msr(X86_MSR_IA32_VMX_PROCBASED_CTLS2),
                      vmcs.Read(VmcsField32::PROCBASED_CTLS2), kProcbasedCtls2PauseLoopExiting, 0);
  if (status == ZX_OK) {
    // From Volume 3, Section 25.1.3: The processor determines the amount of
    // time between this execution of PAUSE and the previous execution of PAUSE
    // at CPL 0. If this amount of time exceeds the value of the VM-execution
    // control field PLE_Gap, the processor considers this execution to be the
    // first execution of PAUSE in a loop. (It also does so for the first
    // execution of PAUSE at CPL 0 after VM entry.)
    //
    // Otherwise, the processor determines the amount of time since the most
    // recent execution of PAUSE that was considered to be the first in a loop.
    // If this amount of time exceeds the value of the VM-execution control
    // field PLE_Window, a VM exit occurs.
    //
    // For purposes of these computations, time is measured based on a counter
    // that runs at the same rate as the timestamp counter (TSC).
    //
    // NOTE: These values are based on KVM, which was based on empirical
    // analysis.
    vmcs.Write(VmcsField32::PLE_GAP, 1u << 7);
    vmcs.Write(VmcsField32::PLE_WINDOW, 1u << 12);
  }
  // From Volume 3, Section 27.5.1: The following bits are not modified: For
  // CR0, ET, CD, NW; [the reserved bits], and any bits that are fixed in VMX
  // operation.
  //
  // Any bit that is not restored must be masked, or the guest will be able to
  // affect the host's cr0. However, we do not need to mask:
  //   * The reserved bits, which will generate GP faults;
  //   * ET, which is fixed to 1 (Volume 3 Section 2.5);
  //   * The bits that are fixed in VMX operation aside from PE and PG for
  //     unrestricted guests, which will generate GP faults (Volume 3 Section
  //     25.3);
  //
  // Additionally, NE is fixed in VMX operation but some guests will attempt to
  // clear it without handling the GP fault. So it should also be masked.
  vmcs.Write(VmcsFieldXX::CR0_GUEST_HOST_MASK, X86_CR0_ET | X86_CR0_NE | X86_CR0_NW | X86_CR0_CD);

  // From Volume 3, Section 9.1.1: Following power-up, The state of control
  // register CR0 is 60000010H (CD and ET are set.)
  vmcs.Write(VmcsFieldXX::CR0_READ_SHADOW, X86_CR0_ET);

  // Mask access to CR4.
  vmcs.Write(VmcsFieldXX::CR4_GUEST_HOST_MASK, X86_CR4_VMXE);
  vmcs.Write(VmcsFieldXX::CR4_READ_SHADOW, 0);

  // Set host and guest CR3.
  vmcs.Write(VmcsFieldXX::HOST_CR3, x86_get_cr3());
  vmcs.Write(VmcsFieldXX::GUEST_CR3, 0);
  // Do not VM exit on any exception.
  vmcs.Write(VmcsField32::EXCEPTION_BITMAP, 0);
  return zx::ok(ktl::move(*vcpu));
}

NormalVcpu::NormalVcpu(NormalGuest& guest, uint16_t vpid, Thread* thread)
    : Vcpu(guest, vpid, thread) {}

NormalVcpu::~NormalVcpu() {
  local_apic_state_.timer.Cancel();
  auto result = static_cast<NormalGuest&>(guest_).FreeVpid(vpid_);
  DEBUG_ASSERT(result.is_ok());
}

zx_status_t NormalVcpu::Enter(zx_port_packet_t& packet) {
  auto pre_enter = [this](AutoVmcs& vmcs) {
    zx_status_t status = local_apic_maybe_interrupt(&vmcs, &local_apic_state_);
    if (status != ZX_OK) {
      return status;
    }
    // Updates guest system time if the guest subscribed to updates.
    auto& guest = static_cast<NormalGuest&>(guest_);
    pv_clock_update_system_time(&pv_clock_state_, &guest.AddressSpace());
    return ZX_OK;
  };
  auto post_exit = [this](AutoVmcs& vmcs, zx_port_packet_t& packet) {
    auto& guest = static_cast<NormalGuest&>(guest_);
    return vmexit_handler_normal(vmcs, vmx_state_.guest_state, local_apic_state_, pv_clock_state_,
                                 guest.AddressSpace(), guest.Traps(), packet);
  };
  return EnterInternal(std::move(pre_enter), std::move(post_exit), packet);
}

void NormalVcpu::Kick() {
  kicked_.store(true);
  // Cancel any pending or upcoming wait-for-interrupts.
  local_apic_state_.interrupt_tracker.Cancel();

  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  interrupt_cpu(thread_.load(), last_cpu_);
}

void NormalVcpu::Interrupt(uint32_t vector) {
  local_apic_state_.interrupt_tracker.Interrupt(vector);

  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  interrupt_cpu(thread_.load(), last_cpu_);
}

zx_status_t NormalVcpu::WriteState(const zx_vcpu_io_t& io_state) {
  if (Thread::Current::Get() != thread_) {
    return ZX_ERR_BAD_STATE;
  }
  if ((io_state.access_size != 1) && (io_state.access_size != 2) && (io_state.access_size != 4)) {
    return ZX_ERR_INVALID_ARGS;
  }
  static_assert(sizeof(vmx_state_.guest_state.rax) >= 4);
  memcpy(&vmx_state_.guest_state.rax, io_state.data, io_state.access_size);
  return ZX_OK;
}

// static
zx::status<ktl::unique_ptr<Vcpu>> DirectVcpu::Create(DirectGuest& guest, zx_vaddr_t entry) {
  auto vcpu = Vcpu::Create<DirectVcpu>(guest, DirectGuest::kGlobalAspaceVpid, entry);
  if (vcpu.is_error()) {
    return vcpu.take_error();
  }
  AutoVmcs vmcs(vcpu->vmcs_page_.PhysicalAddress());
  // Mask access to CR0.
  vmcs.Write(VmcsFieldXX::CR0_GUEST_HOST_MASK, X86_CR0_PE | X86_CR0_ET | X86_CR0_NE | X86_CR0_WP |
                                                   X86_CR0_NW | X86_CR0_CD | X86_CR0_PG);
  vmcs.Write(VmcsFieldXX::CR0_READ_SHADOW,
             X86_CR0_PE | X86_CR0_ET | X86_CR0_NE | X86_CR0_WP | X86_CR0_PG);
  // Mask access to CR4.
  vmcs.Write(VmcsFieldXX::CR4_GUEST_HOST_MASK,
             X86_CR4_PAE | X86_CR4_PGE | X86_CR4_OSFXSR | X86_CR4_VMXE | X86_CR4_OSXSAVE);
  vmcs.Write(VmcsFieldXX::CR4_READ_SHADOW,
             X86_CR4_PAE | X86_CR4_PGE | X86_CR4_OSFXSR | X86_CR4_VMXE | X86_CR4_OSXSAVE);
  // Set CR3 to `user_aspace_`.
  const paddr_t table_phys = guest.user_aspace().arch_aspace().arch_table_phys();
  vmcs.Write(VmcsFieldXX::HOST_CR3, table_phys);
  vmcs.Write(VmcsFieldXX::GUEST_CR3, table_phys);
  // VM exit on double fault and page fault exceptions.
  vmcs.Write(VmcsField32::EXCEPTION_BITMAP,
             (1u << X86_INT_DOUBLE_FAULT) | (1u << X86_INT_PAGE_FAULT));
  return zx::ok(ktl::move(*vcpu));
}

DirectVcpu::DirectVcpu(DirectGuest& guest, uint16_t vpid, Thread* thread)
    : Vcpu(guest, vpid, thread) {}

VmAspace& DirectVcpu::SwitchAspace(VmAspace& aspace) {
  auto* thread = Thread::Current::Get();
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  VmAspace* old_aspace = thread->switch_aspace(&aspace);
  vmm_context_switch(old_aspace, &aspace);
  return *old_aspace;
}

zx_status_t DirectVcpu::Enter(zx_port_packet_t& packet) {
  auto& guest = static_cast<DirectGuest&>(guest_);
  auto pre_enter = [this](AutoVmcs& vmcs) mutable {
    if (fs_base_ != 0) {
      vmcs.Write(VmcsFieldXX::GUEST_FS_BASE, fs_base_);
      fs_base_ = 0;
    }
    return ZX_OK;
  };
  auto post_exit = [this, &guest](AutoVmcs& vmcs, zx_port_packet_t& packet) {
    return vmexit_handler_direct(vmcs, vmx_state_.guest_state, guest.user_aspace(), fs_base_,
                                 packet);
  };
  VmAspace& host_user_aspace = SwitchAspace(guest.user_aspace());
  zx_status_t status = EnterInternal(std::move(pre_enter), std::move(post_exit), packet);
  SwitchAspace(host_user_aspace);
  return status;
}

void DirectVcpu::Kick() {
  kicked_.store(true);
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  interrupt_cpu(thread_.load(), last_cpu_);
}
