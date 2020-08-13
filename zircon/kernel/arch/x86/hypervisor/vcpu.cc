// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <lib/ktrace.h>
#include <zircon/syscalls/hypervisor.h>

#include <new>

#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/x86/platform_access.h>
#include <arch/x86/pv.h>
#include <fbl/auto_call.h>
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

static constexpr uint32_t kInterruptInfoValid = 1u << 31;
static constexpr uint32_t kInterruptInfoDeliverErrorCode = 1u << 11;
static constexpr uint32_t kInterruptTypeNmi = 2u << 8;
static constexpr uint32_t kInterruptTypeHardwareException = 3u << 8;
static constexpr uint32_t kInterruptTypeSoftwareException = 6u << 8;
static constexpr uint16_t kBaseProcessorVpid = 1;

static zx_status_t invept(InvEpt invalidation, uint64_t eptp) {
  uint8_t err;
  uint64_t descriptor[] = {eptp, 0};

  __asm__ __volatile__("invept %[descriptor], %[invalidation];" VMX_ERR_CHECK(err)
                       : [err] "=r"(err)
                       : [descriptor] "m"(descriptor), [invalidation] "r"(invalidation)
                       : "cc");

  return err ? ZX_ERR_INTERNAL : ZX_OK;
}

static zx_status_t vmptrld(paddr_t pa) {
  uint8_t err;

  __asm__ __volatile__("vmptrld %[pa];" VMX_ERR_CHECK(err)
                       : [err] "=r"(err)
                       : [pa] "m"(pa)
                       : "cc", "memory");

  return err ? ZX_ERR_INTERNAL : ZX_OK;
}

static zx_status_t vmclear(paddr_t pa) {
  uint8_t err;

  __asm__ __volatile__("vmclear %[pa];" VMX_ERR_CHECK(err)
                       : [err] "=r"(err)
                       : [pa] "m"(pa)
                       : "cc", "memory");

  return err ? ZX_ERR_INTERNAL : ZX_OK;
}

static uint64_t vmread(uint64_t field) {
  uint8_t err;
  uint64_t val;

  __asm__ __volatile__("vmread %[field], %[val];" VMX_ERR_CHECK(err)
                       : [err] "=r"(err), [val] "=m"(val)
                       : [field] "r"(field)
                       : "cc");

  DEBUG_ASSERT(err == ZX_OK);
  return val;
}

static void vmwrite(uint64_t field, uint64_t val) {
  uint8_t err;

  __asm__ __volatile__("vmwrite %[val], %[field];" VMX_ERR_CHECK(err)
                       : [err] "=r"(err)
                       : [val] "r"(val), [field] "r"(field)
                       : "cc");

  DEBUG_ASSERT(err == ZX_OK);
}

AutoVmcs::AutoVmcs(paddr_t vmcs_address) : vmcs_address_(vmcs_address) {
  DEBUG_ASSERT(!arch_ints_disabled());
  int_state_ = arch_interrupt_save();
  __UNUSED zx_status_t status = vmptrld(vmcs_address_);
  arch_set_blocking_disallowed(true);
  DEBUG_ASSERT(status == ZX_OK);
}

AutoVmcs::~AutoVmcs() {
  DEBUG_ASSERT(arch_ints_disabled());
  if (vmcs_address_) {
    arch_set_blocking_disallowed(false);
  }
  arch_interrupt_restore(int_state_);
}

void AutoVmcs::Invalidate() {
  if (vmcs_address_) {
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
    : thread_(Thread::Current::Get()), prev_affinity_(thread_->scheduler_state().hard_affinity()) {}

AutoPin::~AutoPin() { thread_->SetCpuAffinity(prev_affinity_); }

static uint64_t ept_pointer(paddr_t pml4_address) {
  return
      // Physical address of the PML4 page, page aligned.
      pml4_address |
      // Use write-back memory type for paging structures.
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

bool cr0_is_invalid(AutoVmcs* vmcs, uint64_t cr0_value) {
  uint64_t check_value = cr0_value;
  // From Volume 3, Section 26.3.1.1: PE and PG bits of CR0 are not checked when unrestricted
  // guest is enabled. Set both here to avoid clashing with X86_MSR_IA32_VMX_CR0_FIXED1.
  if (vmcs->Read(VmcsField32::PROCBASED_CTLS2) & kProcbasedCtls2UnrestrictedGuest) {
    check_value |= X86_CR0_PE | X86_CR0_PG;
  }
  return cr_is_invalid(check_value, X86_MSR_IA32_VMX_CR0_FIXED0, X86_MSR_IA32_VMX_CR0_FIXED1);
}

static zx_status_t vmcs_init(paddr_t vmcs_address, uint16_t vpid, uintptr_t entry,
                             paddr_t msr_bitmaps_address, paddr_t pml4_address, VmxState* vmx_state,
                             VmxPage* host_msr_page, VmxPage* guest_msr_page) {
  zx_status_t status = vmclear(vmcs_address);
  if (status != ZX_OK) {
    return status;
  }

  AutoVmcs vmcs(vmcs_address);
  // Setup secondary processor-based VMCS controls.
  status =
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
                          // Enable unrestricted guest.
                          kProcbasedCtls2UnrestrictedGuest,
                      0);
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
                          kProcbasedCtlsProcbasedCtls2,
                      // Disable VM exit on CR3 load.
                      kProcbasedCtlsCr3LoadExiting |
                          // Disable VM exit on CR3 store.
                          kProcbasedCtlsCr3StoreExiting |
                          // Disable VM exit on CR8 load.
                          kProcbasedCtlsCr8LoadExiting |
                          // Disable VM exit on CR8 store.
                          kProcbasedCtlsCr8StoreExiting);
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

  // Setup VM-entry VMCS controls.
  // Load the guest IA32_PAT MSR and IA32_EFER MSR on entry.
  uint32_t entry_ctls = kEntryCtlsLoadIa32Pat | kEntryCtlsLoadIa32Efer;
  if (vpid == kBaseProcessorVpid) {
    // On the BSP, go straight to IA32E mode on entry.
    entry_ctls |= kEntryCtlsIa32eMode;
  }
  status = vmcs.SetControl(VmcsField32::ENTRY_CTLS, read_msr(X86_MSR_IA32_VMX_TRUE_ENTRY_CTLS),
                           read_msr(X86_MSR_IA32_VMX_ENTRY_CTLS), entry_ctls, 0);
  if (status != ZX_OK) {
    return status;
  }

  // Enable use of PAUSE-loop exiting if available.
  status =
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
  uint64_t cr0 = X86_CR0_PE |  // Enable protected mode
                 X86_CR0_PG |  // Enable paging
                 X86_CR0_NE;   // Enable internal x87 exception handling
  if (vpid != kBaseProcessorVpid) {
    // Disable protected mode and paging on secondary VCPUs.
    cr0 &= ~(X86_CR0_PE | X86_CR0_PG);
  }
  if (cr0_is_invalid(&vmcs, cr0)) {
    return ZX_ERR_BAD_STATE;
  }
  vmcs.Write(VmcsFieldXX::GUEST_CR0, cr0);

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
  vmcs.Write(VmcsFieldXX::CR0_GUEST_HOST_MASK, X86_CR0_NE | X86_CR0_NW | X86_CR0_CD);

  // From Volume 3, Section 9.1.1: Following power-up, The state of control register CR0 is
  // 60000010H (CD and ET are set.)
  vmcs.Write(VmcsFieldXX::CR0_READ_SHADOW, X86_CR0_CD | X86_CR0_ET);

  uint64_t cr4 = X86_CR4_VMXE;  // Enable VMX
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

  uint32_t cs_access_rights =
      kGuestXxAccessRightsDefault | kGuestXxAccessRightsTypeE | kGuestXxAccessRightsTypeCode;
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
zx_status_t Vcpu::Create(Guest* guest, zx_vaddr_t entry, ktl::unique_ptr<Vcpu>* out) {
  hypervisor::GuestPhysicalAddressSpace* gpas = guest->AddressSpace();
  if (entry >= gpas->size()) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint16_t vpid;
  zx_status_t status = guest->AllocVpid(&vpid);
  if (status != ZX_OK) {
    return status;
  }
  auto auto_call = fbl::MakeAutoCall([guest, vpid]() { guest->FreeVpid(vpid); });

  Thread* thread = Thread::Current::Get();
  if (thread->vcpu()) {
    return ZX_ERR_BAD_STATE;
  }

  fbl::AllocChecker ac;
  ktl::unique_ptr<Vcpu> vcpu(new (&ac) Vcpu(guest, vpid, thread));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto_call.cancel();

  status = vcpu->local_apic_state_.interrupt_tracker.Init();
  if (status != ZX_OK) {
    return status;
  }

  vcpu->pv_clock_state_.is_stable = x86_hypervisor_has_pv_clock()
                                        ? pv_clock_is_stable()
                                        : x86_feature_test(X86_FEATURE_INVAR_TSC);

  VmxInfo vmx_info;
  status = vcpu->host_msr_page_.Alloc(vmx_info, 0);
  if (status != ZX_OK) {
    return status;
  }

  status = vcpu->guest_msr_page_.Alloc(vmx_info, 0);
  if (status != ZX_OK) {
    return status;
  }

  status = vcpu->vmcs_page_.Alloc(vmx_info, 0);
  if (status != ZX_OK) {
    return status;
  }

  // Initialize a 64-byte aligned XSAVE area. The guest may enable any non-supervisor state so we
  // allocate the maximum possible size. It is then initialized with the minimum possible XSTATE
  // bit vector, which corresponds to the guest's initial state.
  vcpu->guest_extended_registers_.reset(new (std::align_val_t(64), &ac)
                                            uint8_t[x86_extended_register_max_size()]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  x86_extended_register_init_state_from_bv(vcpu->guest_extended_registers_.get(),
                                           X86_XSAVE_STATE_BIT_X87);

  VmxRegion* region = vcpu->vmcs_page_.VirtualAddress<VmxRegion>();
  region->revision_id = vmx_info.revision_id;
  zx_paddr_t pml4_address = gpas->arch_aspace()->arch_table_phys();
  status =
      vmcs_init(vcpu->vmcs_page_.PhysicalAddress(), vpid, entry, guest->MsrBitmapsAddress(),
                pml4_address, &vcpu->vmx_state_, &vcpu->host_msr_page_, &vcpu->guest_msr_page_);
  if (status != ZX_OK) {
    return status;
  }

  *out = ktl::move(vcpu);
  return ZX_OK;
}

Vcpu::Vcpu(Guest* guest, uint16_t vpid, Thread* thread)
    : guest_(guest),
      vpid_(vpid),
      thread_(thread),
      last_cpu_(thread->LastCpu()),
      vmx_state_(/* zero-init */) {
  thread->set_vcpu(true);
  // We have to disable thread safety analysis because it's not smart enough to
  // realize that SetMigrateFn will always be called with the ThreadLock.
  thread->SetMigrateFn([this](Thread* thread, auto stage)
                           TA_NO_THREAD_SAFETY_ANALYSIS { MigrateCpu(thread, stage); });
}

Vcpu::~Vcpu() {
  local_apic_state_.timer.Cancel();

  {
    // Taking the ThreadLock guarantees that thread_ isn't going to be freed
    // while we access it.
    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
    Thread* thread = thread_.load();
    if (thread != nullptr) {
      thread->set_vcpu(false);
      // Clear the migration function, so that |thread_| does not reference
      // |this| after destruction of the VCPU.
      thread->SetMigrateFnLocked(nullptr);
    }
  }

  if (vmcs_page_.IsAllocated()) {
    // The destructor may be called from a different thread, therefore we must
    // pin the current thread to the same CPU as the VCPU thread.
    AutoPin pin(vpid_);
    __UNUSED zx_status_t status = vmclear(vmcs_page_.PhysicalAddress());
    DEBUG_ASSERT(status == ZX_OK);
  }

  __UNUSED zx_status_t status = guest_->FreeVpid(vpid_);
  DEBUG_ASSERT(status == ZX_OK);
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
      __UNUSED zx_status_t status = vmclear(vmcs_page_.PhysicalAddress());
      DEBUG_ASSERT(status == ZX_OK);
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
      // We set |resume| to false so that |vmx_enter| will call VMLAUNCH when
      // entering the the guest, instead of VMRESUME.
      vmx_state_.resume = false;

      // Load the VMCS on the destination processor.
      __UNUSED zx_status_t status = vmptrld(vmcs_page_.PhysicalAddress());
      DEBUG_ASSERT(status == ZX_OK);

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

      // Invalidate TLB mappings for the EPT.
      zx_paddr_t pml4_address = guest_->AddressSpace()->arch_aspace()->arch_table_phys();
      invept(InvEpt::SINGLE_CONTEXT, ept_pointer(pml4_address));

      // After thread migration, update the |last_cpu_| for Vcpu::Interrupt().
      last_cpu_.store(thread->LastCpuLocked());
      break;
    }
    case Thread::MigrateStage::Exiting: {
      // When the thread is exiting, set |last_cpu_| to INVALID_CPU and
      // |thread_| to nullptr.
      last_cpu_.store(INVALID_CPU);
      thread_.store(nullptr);
      break;
    }
  }
}

void Vcpu::RestoreGuestExtendedRegisters(Thread* thread, uint64_t guest_cr4) {
  DEBUG_ASSERT(arch_ints_disabled());
  if (!x86_xsave_supported()) {
    // Save host and restore guest x87/SSE state with fxrstor/fxsave.
    x86_extended_register_save_state(thread->arch().extended_register_state);
    x86_extended_register_restore_state(guest_extended_registers_.get());
    return;
  }

  // Save host state.
  vmx_state_.host_state.xcr0 = x86_xgetbv(0);
  x86_extended_register_save_state(thread->arch().extended_register_state);

  // Restore guest state.
  x86_xsetbv(0, x86_extended_xcr0_component_bitmap());
  x86_extended_register_restore_state(guest_extended_registers_.get());
  x86_xsetbv(0, vmx_state_.guest_state.xcr0);
}

void Vcpu::SaveGuestExtendedRegisters(Thread* thread, uint64_t guest_cr4) {
  DEBUG_ASSERT(arch_ints_disabled());
  if (!x86_xsave_supported()) {
    // Save host and restore guest x87/SSE state with fxrstor/fxsave.
    x86_extended_register_save_state(guest_extended_registers_.get());
    x86_extended_register_restore_state(thread->arch().extended_register_state);
    return;
  }

  // Save guest state.
  vmx_state_.guest_state.xcr0 = x86_xgetbv(0);
  x86_xsetbv(0, x86_extended_xcr0_component_bitmap());
  x86_extended_register_save_state(guest_extended_registers_.get());

  // Restore host state.
  x86_xsetbv(0, vmx_state_.host_state.xcr0);
  x86_extended_register_restore_state(thread->arch().extended_register_state);
}

// Injects an interrupt into the guest, if there is one pending.
static zx_status_t local_apic_maybe_interrupt(AutoVmcs* vmcs, LocalApicState* local_apic_state) {
  // Since hardware generated exceptions are delivered to the guest directly, the only exceptions
  // we see here are those we generate in the VMM, e.g. GP faults in vmexit handlers. Therefore
  // we simplify interrupt priority to 1) NMIs, 2) interrupts, and 3) generated exceptions. See
  // Volume 3, Section 6.9, Table 6-2.
  uint32_t vector = X86_INT_COUNT;
  hypervisor::InterruptType type = local_apic_state->interrupt_tracker.TryPop(X86_INT_NMI);
  if (type != hypervisor::InterruptType::INACTIVE) {
    vector = X86_INT_NMI;
  } else {
    // Pop scans vectors from highest to lowest, which will correctly pop interrupts before
    // exceptions. All vectors <= X86_INT_VIRT except the NMI vector are exceptions.
    type = local_apic_state->interrupt_tracker.Pop(&vector);
    if (type == hypervisor::InterruptType::INACTIVE) {
      return ZX_OK;
    }
    // If type isn't inactive, then Pop should have initialized vector to a valid value.
    DEBUG_ASSERT(vector != X86_INT_COUNT);
  }

  // NMI injection is blocked if an NMI is already being serviced (Volume 3, Section 24.4.2,
  // Table 24-3), and mov ss blocks *all* interrupts (Volume 2 Section 4.3 MOV-Move instruction).
  // Note that the IF flag does not affect NMIs (Volume 3, Section 6.8.1).
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
             (vector == X86_INT_NMI && !can_inject_nmi())

  ) {
    local_apic_state->interrupt_tracker.Track(vector, type);
    // If interrupts are disabled, we set VM exit on interrupt enable.
    vmcs->InterruptWindowExiting(true);
    return ZX_OK;
  }

  // If the vector is non-maskable or interrupts are enabled, we inject an interrupt.
  vmcs->IssueInterrupt(vector);

  // Volume 3, Section 6.9: Lower priority exceptions are discarded; lower priority interrupts are
  // held pending. Discarded exceptions are re-generated when the interrupt handler returns
  // execution to the point in the program or task where the exceptions and/or interrupts
  // occurred.
  local_apic_state->interrupt_tracker.Clear(0, X86_INT_NMI);
  local_apic_state->interrupt_tracker.Clear(X86_INT_NMI + 1, X86_INT_VIRT + 1);

  return ZX_OK;
}

zx_status_t Vcpu::Resume(zx_port_packet_t* packet) {
  Thread* current_thread = Thread::Current::Get();
  if (current_thread != thread_) {
    return ZX_ERR_BAD_STATE;
  }

  zx_status_t status;
  do {
    AutoVmcs vmcs(vmcs_page_.PhysicalAddress());
    status = local_apic_maybe_interrupt(&vmcs, &local_apic_state_);
    if (status != ZX_OK) {
      return status;
    }

    RestoreGuestExtendedRegisters(current_thread, vmcs.Read(VmcsFieldXX::GUEST_CR4));

    // Updates guest system time if the guest subscribed to updates.
    pv_clock_update_system_time(&pv_clock_state_, guest_->AddressSpace());

    if (x86_cpu_should_l1d_flush_on_vmentry()) {
      // L1TF: Flush L1D$ before entering vCPU. If the CPU is affected by MDS, also flush
      // microarchitectural buffers.
      write_msr(X86_MSR_IA32_FLUSH_CMD, 1);
    } else if (x86_cpu_should_md_clear_on_user_return()) {
      // MDS: If the processor is not affected by L1TF but is affected by MDS or TAA,
      // flush microarchitectural buffers.
      mds_buff_overwrite();
    }

    ktrace(TAG_VCPU_ENTER, 0, 0, 0, 0);
    GUEST_STATS_INC(vm_entries);
    running_.store(true);
    status = vmx_enter(&vmx_state_);
    running_.store(false);
    GUEST_STATS_INC(vm_exits);

    SaveGuestExtendedRegisters(current_thread, vmcs.Read(VmcsFieldXX::GUEST_CR4));

    if (!x86_get_disable_spec_mitigations()) {
      // Spectre V2: Ensure that code executed in the VM guest cannot influence either
      // return address predictions or indirect branch prediction in the host.
      //
      // TODO(fxb/33667): We may be able to avoid the IBPB here; the kernel is either
      // built with a retpoline or has Enhanced IBRS enabled. We currently execute an
      // IBPB on context-switch to a new aspace. The IBPB is currently only here to
      // protect hypervisor user threads.
      MsrAccess msr;
      x86_ras_fill();
      if (x86_cpu_has_ibpb()) {
        x86_cpu_ibpb(&msr);
      }
    }

    if (status != ZX_OK) {
      ktrace_vcpu_exit(VCPU_FAILURE, vmcs.Read(VmcsFieldXX::GUEST_RIP));
      uint64_t error = vmcs.Read(VmcsField32::INSTRUCTION_ERROR);
      dprintf(INFO, "VCPU resume failed: %#lx\n", error);
    } else {
      vmx_state_.resume = true;
      status = vmexit_handler(&vmcs, &vmx_state_.guest_state, &local_apic_state_, &pv_clock_state_,
                              guest_->AddressSpace(), guest_->Traps(), packet);
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

void Vcpu::Interrupt(uint32_t vector, hypervisor::InterruptType type) {
  local_apic_state_.interrupt_tracker.Interrupt(vector, type);
  if (running_.load()) {
    mp_interrupt(MP_IPI_TARGET_MASK, cpu_num_to_mask(last_cpu_.load()));
  }
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

zx_status_t Vcpu::ReadState(zx_vcpu_state_t* vcpu_state) {
  if (Thread::Current::Get() != thread_) {
    return ZX_ERR_BAD_STATE;
  }
  register_copy(vcpu_state, vmx_state_.guest_state);
  AutoVmcs vmcs(vmcs_page_.PhysicalAddress());
  vcpu_state->rsp = vmcs.Read(VmcsFieldXX::GUEST_RSP);
  vcpu_state->rflags = vmcs.Read(VmcsFieldXX::GUEST_RFLAGS) & X86_FLAGS_USER;
  return ZX_OK;
}

zx_status_t Vcpu::WriteState(const zx_vcpu_state_t& vcpu_state) {
  if (Thread::Current::Get() != thread_) {
    return ZX_ERR_BAD_STATE;
  }
  register_copy(&vmx_state_.guest_state, vcpu_state);
  AutoVmcs vmcs(vmcs_page_.PhysicalAddress());
  vmcs.Write(VmcsFieldXX::GUEST_RSP, vcpu_state.rsp);
  if (vcpu_state.rflags & X86_FLAGS_RESERVED_ONES) {
    const uint64_t rflags = vmcs.Read(VmcsFieldXX::GUEST_RFLAGS);
    const uint64_t user_flags = (rflags & ~X86_FLAGS_USER) | (vcpu_state.rflags & X86_FLAGS_USER);
    vmcs.Write(VmcsFieldXX::GUEST_RFLAGS, user_flags);
  }
  return ZX_OK;
}

zx_status_t Vcpu::WriteState(const zx_vcpu_io_t& io_state) {
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
