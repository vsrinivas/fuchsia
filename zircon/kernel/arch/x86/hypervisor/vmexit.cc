// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <inttypes.h>
#include <lib/ktrace.h>
#include <platform.h>
#include <string.h>
#include <trace.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <arch/hypervisor.h>
#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <arch/x86/hypervisor/invalidate.h>
#include <arch/x86/interrupts.h>
#include <arch/x86/mmu.h>
#include <arch/x86/pv.h>
#include <hypervisor/interrupt_tracker.h>
#include <hypervisor/ktrace.h>
#include <kernel/percpu.h>
#include <kernel/stats.h>
#include <ktl/algorithm.h>
#include <platform/pc/timer.h>
#include <vm/fault.h>
#include <vm/physmap.h>
#include <vm/pmm.h>

#include "pv_priv.h"
#include "vcpu_priv.h"
#include "vmcall_priv.h"
#include "vmexit_priv.h"

#include <ktl/enforce.h>

#define LOCAL_TRACE 0

extern "C" void x86_call_external_interrupt_handler(uint64_t vector);

namespace {

constexpr uint64_t kLocalApicPhysBase =
    APIC_PHYS_BASE | IA32_APIC_BASE_XAPIC_ENABLE | IA32_APIC_BASE_X2APIC_ENABLE;

constexpr uint64_t kX2ApicMsrBase = 0x800;
constexpr uint64_t kX2ApicMsrMax = 0x83f;

constexpr uint64_t kMiscEnableFastStrings = 1u << 0;

constexpr uint32_t kFirstExtendedStateComponent = 2;
constexpr uint32_t kLastExtendedStateComponent = 9;
// From Volume 1, Section 13.4.
constexpr uint32_t kXsaveLegacyRegionSize = 512;
constexpr uint32_t kXsaveHeaderSize = 64;

// NOTE: x86 instructions are guaranteed to be 15 bytes or fewer.
constexpr uint8_t kMaxInstructionSize = 15;

constexpr char kHypVendorId[] = "KVMKVMKVM\0\0\0";
static_assert(sizeof(kHypVendorId) - 1 == 12, "Vendor ID must be 12 characters long");

constexpr uint64_t kKvmFeatureNoIoDelay = 1u << 1;

void dump_guest_state(const GuestState& guest_state, const ExitInfo& exit_info) {
  dprintf(INFO, " RAX: %#18lx  RCX: %#18lx  RDX: %#18lx  RBX: %#18lx\n", guest_state.rax,
          guest_state.rcx, guest_state.rdx, guest_state.rbx);
  dprintf(INFO, " RSP:  xxxxxxxx xxxxxxxx  RBP: %#18lx  RSI: %#18lx  RDI: %#18lx\n",
          guest_state.rbp, guest_state.rsi, guest_state.rdi);
  dprintf(INFO, "  R8: %#18lx   R9: %#18lx  R10: %#18lx  R11: %#18lx\n", guest_state.r8,
          guest_state.r9, guest_state.r10, guest_state.r11);
  dprintf(INFO, " R12: %#18lx  R13: %#18lx  R14: %#18lx  R15: %#18lx\n", guest_state.r12,
          guest_state.r13, guest_state.r14, guest_state.r15);
  dprintf(INFO, " RIP: %#18lx  CR2: %#18lx XCR0: %#18lx\n", exit_info.guest_rip, guest_state.cr2,
          guest_state.xcr0);

  dprintf(INFO, "entry failure: %d\n", exit_info.entry_failure);
  dprintf(INFO, "exit instruction length: %#x\n", exit_info.exit_instruction_length);
}

void next_rip(const ExitInfo& exit_info, AutoVmcs& vmcs) {
  vmcs.Write(VmcsFieldXX::GUEST_RIP, exit_info.guest_rip + exit_info.exit_instruction_length);

  // Clear any flags blocking interrupt injection for a single instruction.
  uint32_t guest_interruptibility = vmcs.Read(VmcsField32::GUEST_INTERRUPTIBILITY_STATE);
  uint32_t new_interruptibility =
      guest_interruptibility & ~(kInterruptibilityStiBlocking | kInterruptibilityMovSsBlocking);
  if (new_interruptibility != guest_interruptibility) {
    vmcs.Write(VmcsField32::GUEST_INTERRUPTIBILITY_STATE, new_interruptibility);
  }
}

zx_status_t handle_exception_or_nmi(AutoVmcs& vmcs, VmAspace& user_aspace) {
  const ExitInterruptionInfo int_info(vmcs);
  DEBUG_ASSERT(int_info.valid);
  // Only handle page faults, everything else should terminate the VCPU.
  if (int_info.interruption_type != InterruptionType::HARDWARE_EXCEPTION ||
      int_info.vector != X86_INT_PAGE_FAULT) {
    return ZX_ERR_BAD_STATE;
  }
  // Page fault resume should not end up here.
  if (Thread::Current::Get()->arch().page_fault_resume != 0) {
    return ZX_ERR_INTERNAL;
  }

  const zx_vaddr_t guest_vaddr = vmcs.Read(VmcsFieldXX::EXIT_QUALIFICATION);
  DEBUG_ASSERT(int_info.error_code_valid);
  const PageFaultInfo pf_info(vmcs.Read(VmcsField32::EXIT_INTERRUPTION_ERROR_CODE));

  // We may have to block when handling the page fault.
  vmcs.Invalidate();
  return vmm_page_fault_handler(guest_vaddr, pf_info.flags);
}

zx_status_t handle_external_interrupt(AutoVmcs& vmcs) {
  const ExitInterruptionInfo int_info(vmcs);
  DEBUG_ASSERT(int_info.valid);
  DEBUG_ASSERT(int_info.interruption_type == InterruptionType::EXTERNAL_INTERRUPT);
  vmcs.Invalidate();
  x86_call_external_interrupt_handler(int_info.vector);
  return ZX_OK;
}

zx_status_t handle_interrupt_window(AutoVmcs& vmcs) {
  vmcs.InterruptWindowExiting(false);
  return ZX_OK;
}

// From Volume 2, Section 3.2, Table 3-8  "Processor Extended State Enumeration
// Main Leaf (EAX = 0DH, ECX = 0)".
//
// Bits 31-00: Maximum size (bytes, from the beginning of the XSAVE/XRSTOR save
// area) required by enabled features in XCR0. May be different than ECX if some
// features at the end of the XSAVE save area are not enabled.
zx_status_t compute_xsave_size(uint64_t guest_xcr0, uint32_t& xsave_size) {
  xsave_size = kXsaveLegacyRegionSize + kXsaveHeaderSize;
  for (uint32_t i = kFirstExtendedStateComponent; i <= kLastExtendedStateComponent; ++i) {
    cpuid_leaf leaf;
    if (!(guest_xcr0 & (1 << i))) {
      continue;
    }
    if (!x86_get_cpuid_subleaf(X86_CPUID_XSAVE, i, &leaf)) {
      return ZX_ERR_INTERNAL;
    }
    if (leaf.a == 0 && leaf.b == 0 && leaf.c == 0 && leaf.d == 0) {
      continue;
    }
    const uint32_t component_offset = leaf.b;
    const uint32_t component_size = leaf.a;
    xsave_size = component_offset + component_size;
  }
  return ZX_OK;
}

zx_status_t handle_cpuid(const ExitInfo& exit_info, AutoVmcs& vmcs, GuestState& guest_state) {
  const uint32_t leaf = guest_state.eax();
  const uint32_t subleaf = guest_state.ecx();

  next_rip(exit_info, vmcs);
  switch (leaf) {
    case X86_CPUID_BASE:
    case X86_CPUID_EXT_BASE:
      cpuid(leaf, reinterpret_cast<uint32_t*>(&guest_state.rax),
            reinterpret_cast<uint32_t*>(&guest_state.rbx),
            reinterpret_cast<uint32_t*>(&guest_state.rcx),
            reinterpret_cast<uint32_t*>(&guest_state.rdx));
      return ZX_OK;
    case X86_CPUID_BASE + 1 ... MAX_SUPPORTED_CPUID:
    case X86_CPUID_EXT_BASE + 1 ... MAX_SUPPORTED_CPUID_EXT:
      cpuid_c(leaf, subleaf, reinterpret_cast<uint32_t*>(&guest_state.rax),
              reinterpret_cast<uint32_t*>(&guest_state.rbx),
              reinterpret_cast<uint32_t*>(&guest_state.rcx),
              reinterpret_cast<uint32_t*>(&guest_state.rdx));
      switch (leaf) {
        case X86_CPUID_MODEL_FEATURES:
          // Override the initial local APIC ID. From Vol 2, Table 3-8.
          guest_state.rbx &= ~(0xff << 24);
          guest_state.rbx |= (vmcs.Read(VmcsField16::VPID) - 1) << 24;
          // Enable the hypervisor bit.
          guest_state.rcx |= 1u << X86_FEATURE_HYPERVISOR.bit;
          // Enable the x2APIC bit.
          guest_state.rcx |= 1u << X86_FEATURE_X2APIC.bit;
          // Always enable TSC deadline (this doesn't depend on the host feature).
          guest_state.rcx |= 1u << X86_FEATURE_TSC_DEADLINE.bit;
          // Disable the VMX bit.
          guest_state.rcx &= ~(1u << X86_FEATURE_VMX.bit);
          // Disable the PDCM bit.
          guest_state.rcx &= ~(1u << X86_FEATURE_PDCM.bit);
          // Disable MONITOR/MWAIT.
          guest_state.rcx &= ~(1u << X86_FEATURE_MON.bit);
          // Disable THERM_INTERRUPT and THERM_STATUS MSRs
          guest_state.rcx &= ~(1u << X86_FEATURE_TM2.bit);
          // Enable the SEP (SYSENTER support).
          guest_state.rdx |= 1u << X86_FEATURE_SEP.bit;
          // Disable the Thermal Monitor bit.
          guest_state.rdx &= ~(1u << X86_FEATURE_TM.bit);
          // Disable the THERM_CONTROL_MSR bit.
          guest_state.rdx &= ~(1u << X86_FEATURE_ACPI.bit);
          break;
        case X86_CPUID_TOPOLOGY:
          guest_state.rax = 0;
          guest_state.rbx = 0;
          guest_state.rcx = 0;
          guest_state.rdx = vmcs.Read(VmcsField16::VPID) - 1;
          break;
        case X86_CPUID_XSAVE:
          if (subleaf == 0) {
            uint32_t xsave_size = 0;
            zx_status_t status = compute_xsave_size(guest_state.xcr0, xsave_size);
            if (status != ZX_OK) {
              return status;
            }
            guest_state.rbx = xsave_size;
          } else if (subleaf == 1) {
            guest_state.rax &= ~(1u << 3);
          }
          break;
        case X86_CPUID_THERMAL_AND_POWER:
          // Disable the performance energy bias bit.
          guest_state.rcx &= ~(1u << X86_FEATURE_PERF_BIAS.bit);
          // Disable the hardware coordination feedback bit.
          guest_state.rcx &= ~(1u << X86_FEATURE_HW_FEEDBACK.bit);
          guest_state.rax &= ~(
              // Disable Digital Thermal Sensor
              1u << X86_FEATURE_DTS.bit |
              // Disable Package Thermal Status MSR.
              1u << X86_FEATURE_PTM.bit |
              // Disable THERM_STATUS MSR bits 10/11 & THERM_INTERRUPT MSR bit 24
              1u << X86_FEATURE_PLN.bit |
              // Disable HWP MSRs.
              1u << X86_FEATURE_HWP.bit | 1u << X86_FEATURE_HWP_NOT.bit |
              1u << X86_FEATURE_HWP_ACT.bit | 1u << X86_FEATURE_HWP_PREF.bit |
              // Don't advertise Turbo.
              1u << X86_FEATURE_TURBO.bit | 1u << X86_FEATURE_TURBO_MAX.bit);
          break;
        case X86_CPUID_PERFORMANCE_MONITORING: {
          // Disable all performance monitoring.
          // 31-07 = Reserved 0, 06-00 = 1 if event is not available.
          const uint32_t performance_monitoring_no_events = 0b1111111;
          guest_state.rax = 0;
          guest_state.rbx = performance_monitoring_no_events;
          guest_state.rcx = 0;
          guest_state.rdx = 0;
          break;
        }
        case X86_CPUID_MON:
          // MONITOR/MWAIT are not implemented.
          guest_state.rax = 0;
          guest_state.rbx = 0;
          guest_state.rcx = 0;
          guest_state.rdx = 0;
          break;
        case X86_CPUID_EXTENDED_FEATURE_FLAGS:
          // It's possible when running under KVM in nVMX mode, that host
          // CPUID indicates that invpcid is supported but VMX doesn't allow
          // to enable INVPCID bit in secondary processor based controls.
          // Therefore explicitly clear INVPCID bit in CPUID if the VMX flag
          // wasn't set.
          if ((vmcs.Read(VmcsField32::PROCBASED_CTLS2) & kProcbasedCtls2Invpcid) == 0) {
            guest_state.rbx &= ~(1u << X86_FEATURE_INVPCID.bit);
          }
          // Disable:
          //  * Processor Trace bit
          //  * TSC Adjust bit
          guest_state.rbx &= ~(1u << X86_FEATURE_PT.bit | 1u << X86_FEATURE_TSC_ADJUST.bit);
          // Disable:
          //  * Indirect Branch Prediction Barrier bit
          //  * Single Thread Indirect Branch Predictors bit
          //  * Speculative Store Bypass Disable bit
          // These imply support for the IA32_SPEC_CTRL and IA32_PRED_CMD
          // MSRs, which are not implemented.
          guest_state.rdx &= ~(1u << X86_FEATURE_IBRS_IBPB.bit | 1u << X86_FEATURE_STIBP.bit |
                               1u << X86_FEATURE_SSBD.bit);
          // Disable support for the IA32_ARCH_CAPABILITIES MSR.
          guest_state.rdx &= ~(1u << X86_FEATURE_ARCH_CAPABILITIES.bit);
          // Disable support for the IA32_FLUSH_CMD MSR.
          guest_state.rdx &= ~(1u << X86_FEATURE_L1D_FLUSH.bit);
          break;
      }
      return ZX_OK;
    case X86_CPUID_HYP_VENDOR: {
      // This leaf is commonly used to identify a hypervisor via ebx:ecx:edx.
      auto regs = reinterpret_cast<const uint32_t*>(kHypVendorId);
      // Since Zircon hypervisor disguises itself as KVM, it needs to return
      // in EAX max CPUID function supported by hypervisor. Zero in EAX
      // should be interpreted as 0x40000001. Details are available in the
      // Linux kernel documentation (Documentation/virtual/kvm/cpuid.txt).
      guest_state.rax = X86_CPUID_KVM_FEATURES;
      guest_state.rbx = regs[0];
      guest_state.rcx = regs[1];
      guest_state.rdx = regs[2];
      return ZX_OK;
    }
    case X86_CPUID_KVM_FEATURES:
      // We support KVM clock.
      guest_state.rax = kKvmFeatureClockSourceOld | kKvmFeatureClockSource | kKvmFeatureNoIoDelay;
      guest_state.rbx = 0;
      guest_state.rcx = 0;
      guest_state.rdx = 0;
      return ZX_OK;
    // From Volume 2A, CPUID instruction reference. If the EAX value is outside
    // the range recognized by CPUID then the information for the highest
    // supported base information leaf is returned. Any value in ECX is
    // honored.
    default:
      cpuid_c(MAX_SUPPORTED_CPUID, subleaf, reinterpret_cast<uint32_t*>(&guest_state.rax),
              reinterpret_cast<uint32_t*>(&guest_state.rbx),
              reinterpret_cast<uint32_t*>(&guest_state.rcx),
              reinterpret_cast<uint32_t*>(&guest_state.rdx));
      return ZX_OK;
  }
}

zx_status_t handle_hlt(const ExitInfo& exit_info, AutoVmcs& vmcs,
                       LocalApicState& local_apic_state) {
  next_rip(exit_info, vmcs);
  return local_apic_state.interrupt_tracker.Wait(ZX_TIME_INFINITE, &vmcs).status_value();
}

zx_status_t handle_cr0_write(AutoVmcs& vmcs, uint64_t val, LocalApicState& local_apic_state) {
  // X86_CR0_NE is masked so that guests may write to it, but depending on
  // IA32_VMX_CR0_FIXED0 it might be unsupported in VMX operation to set it to
  // zero. Allow the guest to control its value in CR0_READ_SHADOW but not in
  // GUEST_CR0 so that GUEST_CR0 stays valid.
  uint64_t cr0 = val | X86_CR0_NE;
  if (cr0_is_invalid(vmcs, cr0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // From Volume 3, Table 11-5: CD=0 and NW=1 is an invalid setting and should
  // generate a GP fault.
  if (!(val & X86_CR0_CD) && (val & X86_CR0_NW)) {
    local_apic_state.interrupt_tracker.Interrupt(X86_INT_GP_FAULT);
    return ZX_OK;
  }

  // If CR0.PG is being changed, then invalidate the VPID.
  uint64_t cr0_changed = val ^ vmcs.Read(VmcsFieldXX::GUEST_CR0);
  if (cr0_changed & X86_CR0_PG) {
    uint16_t vpid = vmcs.Read(VmcsField16::VPID);
    invvpid(InvVpid::SINGLE_CONTEXT, vpid, 0);
  }

  // From Volume 3, Section 26.3.2.1: CR0 is loaded from the CR0 field with the
  // exception of the following bits, which are never modified on VM entry: ET
  // (bit 4); reserved bits ...; NW (bit 29) and CD (bit 30). The values of
  // these bits in the CR0 field are ignored.
  //
  // Even though these bits will be ignored on VM entry, to ensure that
  // GUEST_CR0 matches the actual value of CR0 while the guest is running set
  // those bits to match the host values. This is done only to make debugging
  // simpler.
  cr0 &= ~(X86_CR0_NW | X86_CR0_CD);
  cr0 |= X86_CR0_ET;
  vmcs.Write(VmcsFieldXX::GUEST_CR0, cr0);

  // From Volume 3, Section 25.3: For each position corresponding to a bit clear
  // in the CR0 guest/host mask, the destination operand is loaded with the
  // value of the corresponding bit in CR0. For each position corresponding to a
  // bit set in the CR0 guest/host mask, the destination operand is loaded with
  // the value of the corresponding bit in the CR0 read shadow.
  //
  // Allow the guest to control the shadow.
  vmcs.Write(VmcsFieldXX::CR0_READ_SHADOW, val);

  // From Volume 3, Section 26.3.1.1: If CR0.PG and EFER.LME are set then
  // EFER.LMA and the IA-32e mode guest entry control must also be set.
  uint64_t efer = vmcs.Read(VmcsField64::GUEST_IA32_EFER);
  if (!(efer & X86_EFER_LME && cr0 & X86_CR0_PG)) {
    return ZX_OK;
  }
  vmcs.Write(VmcsField64::GUEST_IA32_EFER, efer | X86_EFER_LMA);
  return vmcs.SetControl(VmcsField32::ENTRY_CTLS, read_msr(X86_MSR_IA32_VMX_TRUE_ENTRY_CTLS),
                         read_msr(X86_MSR_IA32_VMX_ENTRY_CTLS), kEntryCtls64bitMode, 0);
}

zx_status_t register_value(AutoVmcs& vmcs, const GuestState& guest_state, uint8_t register_id,
                           uint64_t& out) {
  switch (register_id) {
    // From Intel Volume 3, Table 27-3.
    case 0:
      out = guest_state.rax;
      return ZX_OK;
    case 1:
      out = guest_state.rcx;
      return ZX_OK;
    case 2:
      out = guest_state.rdx;
      return ZX_OK;
    case 3:
      out = guest_state.rbx;
      return ZX_OK;
    case 4:
      out = vmcs.Read(VmcsFieldXX::GUEST_RSP);
      return ZX_OK;
    case 5:
      out = guest_state.rbp;
      return ZX_OK;
    case 6:
      out = guest_state.rsi;
      return ZX_OK;
    case 7:
      out = guest_state.rdi;
      return ZX_OK;
    case 8:
      out = guest_state.r8;
      return ZX_OK;
    case 9:
      out = guest_state.r9;
      return ZX_OK;
    case 10:
      out = guest_state.r10;
      return ZX_OK;
    case 11:
      out = guest_state.r11;
      return ZX_OK;
    case 12:
      out = guest_state.r12;
      return ZX_OK;
    case 13:
      out = guest_state.r13;
      return ZX_OK;
    case 14:
      out = guest_state.r14;
      return ZX_OK;
    case 15:
      out = guest_state.r15;
      return ZX_OK;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

zx_status_t handle_control_register_access(const ExitInfo& exit_info, AutoVmcs& vmcs,
                                           const GuestState& guest_state,
                                           LocalApicState& local_apic_state) {
  const CrAccessInfo cr_access_info(vmcs.Read(VmcsFieldXX::EXIT_QUALIFICATION));
  switch (cr_access_info.access_type) {
    case CrAccessType::MOV_TO_CR: {
      // Handle CR0 only.
      if (cr_access_info.cr_number != 0) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      uint64_t val;
      zx_status_t status = register_value(vmcs, guest_state, cr_access_info.reg, val);
      if (status != ZX_OK) {
        return status;
      }
      status = handle_cr0_write(vmcs, val, local_apic_state);
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

zx_status_t handle_io_instruction(const ExitInfo& exit_info, AutoVmcs& vmcs,
                                  GuestState& guest_state, hypervisor::TrapMap& traps,
                                  zx_port_packet_t& packet) {
  const IoInfo io_info(vmcs.Read(VmcsFieldXX::EXIT_QUALIFICATION));
  if (io_info.string || io_info.repeat) {
    dprintf(INFO, "hypervisor: Unsupported guest IO instruction\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx::result<hypervisor::Trap*> trap = traps.FindTrap(ZX_GUEST_TRAP_IO, io_info.port);
  if (trap.is_error()) {
    dprintf(INFO, "hypervisor: Unhandled guest IO port %s %#x\n", io_info.input ? "read" : "write",
            io_info.port);
    return trap.status_value();
  }
  next_rip(exit_info, vmcs);

  memset(&packet, 0, sizeof(packet));
  packet.key = (*trap)->key();
  packet.type = ZX_PKT_TYPE_GUEST_IO;
  packet.guest_io.port = io_info.port;
  packet.guest_io.access_size = io_info.access_size;
  packet.guest_io.input = io_info.input;
  if (io_info.input) {
    // From Volume 1, Section 3.4.1.1: 32-bit operands generate a 32-bit
    // result, zero-extended to a 64-bit result in the destination general-
    // purpose register.
    if (io_info.access_size == 4) {
      guest_state.rax = 0;
    }
  } else {
    memcpy(packet.guest_io.data, &guest_state.rax, io_info.access_size);
    if ((*trap)->HasPort()) {
      return (*trap)->Queue(packet, &vmcs).status_value();
    }
    // If there was no port for the range, then return to user-space.
  }

  return ZX_ERR_NEXT;
}

zx_status_t handle_apic_rdmsr(const ExitInfo& exit_info, AutoVmcs& vmcs, GuestState& guest_state,
                              LocalApicState& local_apic_state) {
  switch (static_cast<X2ApicMsr>(guest_state.ecx())) {
    case X2ApicMsr::ID:
      next_rip(exit_info, vmcs);
      guest_state.rax = vmcs.Read(VmcsField16::VPID) - 1;
      return ZX_OK;
    case X2ApicMsr::VERSION: {
      next_rip(exit_info, vmcs);
      // We choose 15H as it causes us to be seen as a modern APIC by Linux,
      // and is the highest non-reserved value. See Volume 3 Section 10.4.8.
      const uint32_t version = 0x15;
      const uint32_t max_lvt_entry = 0x6;  // LVT entries minus 1.
      const uint32_t eoi_suppression = 0;  // Disable support for EOI-broadcast suppression.
      guest_state.rax = version | (max_lvt_entry << 16) | (eoi_suppression << 24);
      return ZX_OK;
    }
    case X2ApicMsr::SVR:
      // Spurious interrupt vector resets to 0xff. See Volume 3 Section 10.12.5.1.
      next_rip(exit_info, vmcs);
      guest_state.rax = 0xff;
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
      guest_state.rax = 0;
      return ZX_OK;
    case X2ApicMsr::LVT_LINT0:
    case X2ApicMsr::LVT_LINT1:
    case X2ApicMsr::LVT_THERMAL_SENSOR:
    case X2ApicMsr::LVT_CMCI:
      // LVT registers reset with the mask bit set. See Volume 3 Section 10.12.5.1.
      next_rip(exit_info, vmcs);
      guest_state.rax = LVT_MASKED;
      return ZX_OK;
    case X2ApicMsr::LVT_TIMER:
      next_rip(exit_info, vmcs);
      guest_state.rax = local_apic_state.lvt_timer;
      return ZX_OK;
    default:
      // Issue a general protection fault for write only and unimplemented
      // registers.
      dprintf(INFO, "hypervisor: Unhandled guest x2APIC RDMSR %#lx\n", guest_state.rcx);
      local_apic_state.interrupt_tracker.Interrupt(X86_INT_GP_FAULT);
      return ZX_OK;
  }
}

zx_status_t handle_rdmsr(const ExitInfo& exit_info, AutoVmcs& vmcs, GuestState& guest_state,
                         LocalApicState& local_apic_state) {
  // On execution of rdmsr, ecx specifies the MSR and the result is stored in edx:eax.
  switch (guest_state.ecx()) {
    case X86_MSR_IA32_APIC_BASE: {
      next_rip(exit_info, vmcs);
      uint64_t result = kLocalApicPhysBase;
      if (vmcs.Read(VmcsField16::VPID) == 1) {
        result |= IA32_APIC_BASE_BSP;
      }
      guest_state.SetEdxEax(result);
      return ZX_OK;
    }
    // From Volume 4, Section 2.1, Table 2-2: For now, only enable fast strings.
    case X86_MSR_IA32_MISC_ENABLE:
      next_rip(exit_info, vmcs);
      guest_state.SetEdxEax(read_msr(X86_MSR_IA32_MISC_ENABLE) & kMiscEnableFastStrings);
      return ZX_OK;
    case X86_MSR_DRAM_ENERGY_STATUS:
    case X86_MSR_DRAM_POWER_LIMIT:
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
    case X86_MSR_IA32_TEMPERATURE_TARGET:
    case X86_MSR_PKG_ENERGY_STATUS:
    case X86_MSR_PLATFORM_ENERGY_COUNTER:
    case X86_MSR_PLATFORM_POWER_LIMIT:
    case X86_MSR_PP0_ENERGY_STATUS:
    case X86_MSR_PP0_POWER_LIMIT:
    case X86_MSR_PP1_ENERGY_STATUS:
    case X86_MSR_PP1_POWER_LIMIT:
    case X86_MSR_RAPL_POWER_UNIT:
    // From Volume 3, Section 14.2: We've configured CPUID to report no MPERF/APERF
    // support, but Linux attempts to read stats anyhow. Just ignore it.
    case X86_MSR_PPERF:
    // From Volume 4, Table 2-15: Number of SMI interrupts since boot.
    // We report 0 interrupts.
    case X86_MSR_SMI_COUNT:
      next_rip(exit_info, vmcs);
      guest_state.SetEdxEax(0);
      return ZX_OK;
    case kX2ApicMsrBase ... kX2ApicMsrMax:
      return handle_apic_rdmsr(exit_info, vmcs, guest_state, local_apic_state);
    default:
      dprintf(INFO, "hypervisor: Unhandled guest RDMSR %#lx\n", guest_state.rcx);
      local_apic_state.interrupt_tracker.Interrupt(X86_INT_GP_FAULT);
      return ZX_OK;
  }
}

zx_time_t lvt_deadline(LocalApicState& local_apic_state) {
  if ((local_apic_state.lvt_timer & LVT_TIMER_MODE_MASK) != LVT_TIMER_MODE_ONESHOT &&
      (local_apic_state.lvt_timer & LVT_TIMER_MODE_MASK) != LVT_TIMER_MODE_PERIODIC) {
    return 0;
  }
  uint32_t shift = BITS_SHIFT(local_apic_state.lvt_divide_config, 1, 0) |
                   (BIT_SHIFT(local_apic_state.lvt_divide_config, 3) << 2);
  uint32_t divisor_shift = (shift + 1) & 7;
  int64_t duration_tsc_ticks =
      static_cast<int64_t>(local_apic_state.lvt_initial_count << divisor_shift);
  zx_duration_t duration = convert_raw_tsc_duration_to_nanoseconds(duration_tsc_ticks);
  return zx_time_add_duration(current_time(), duration);
}

void update_timer(LocalApicState& local_apic_state, zx_time_t deadline);

void deadline_callback(Timer* timer, zx_time_t now, void* arg) {
  auto& local_apic_state = *static_cast<LocalApicState*>(arg);
  if (local_apic_state.lvt_timer & LVT_MASKED) {
    return;
  }
  if ((local_apic_state.lvt_timer & LVT_TIMER_MODE_MASK) == LVT_TIMER_MODE_PERIODIC) {
    update_timer(local_apic_state, lvt_deadline(local_apic_state));
  }
  uint8_t vector = local_apic_state.lvt_timer & LVT_TIMER_VECTOR_MASK;
  local_apic_state.interrupt_tracker.Interrupt(vector);
}

void update_timer(LocalApicState& local_apic_state, zx_time_t deadline) {
  local_apic_state.timer.Cancel();
  if (deadline > 0) {
    local_apic_state.timer.SetOneshot(deadline, deadline_callback, &local_apic_state);
  }
}

uint64_t ipi_target_mask(const InterruptCommandRegister& icr, uint16_t self) {
  DEBUG_ASSERT(self < kMaxGuestVcpus);

  switch (icr.destination_shorthand) {
    case InterruptDestinationShorthand::NO_SHORTHAND: {
      // Intel Volume 3, Section 10.12.9: A destination ID value of FFFF_FFFFH
      // is used for broadcast of interrupts in both logical destination and
      // physical destination modes.
      if (icr.destination == kIpiBroadcastDestination) {
        return UINT64_MAX;
      }

      // If an invalid destination was provided, just return the empty mask.
      if (unlikely(icr.destination >= kMaxGuestVcpus)) {
        return 0;
      }

      // Otherwise, generate a mask for the target VCPU.
      return 1u << icr.destination;
    }
    case InterruptDestinationShorthand::SELF:
      return 1u << self;
    case InterruptDestinationShorthand::ALL_INCLUDING_SELF:
      return UINT64_MAX;
    case InterruptDestinationShorthand::ALL_EXCLUDING_SELF:
      return ~(1u << self);
  }

  return 0;
}

zx_status_t handle_ipi(const ExitInfo& exit_info, AutoVmcs& vmcs, const GuestState& guest_state,
                       zx_port_packet& packet) {
  InterruptCommandRegister icr(guest_state.edx(), guest_state.eax());
  if (icr.destination_mode == InterruptDestinationMode::LOGICAL) {
    dprintf(INFO, "hypervisor: Logical IPI destination mode requested by guest is not supported\n");
    return ZX_ERR_NOT_SUPPORTED;
  }
  switch (icr.delivery_mode) {
    case InterruptDeliveryMode::FIXED: {
      uint16_t self = vmcs.Read(VmcsField16::VPID) - 1;
      memset(&packet, 0, sizeof(packet));
      packet.type = ZX_PKT_TYPE_GUEST_VCPU;
      packet.guest_vcpu.type = ZX_PKT_GUEST_VCPU_INTERRUPT;
      packet.guest_vcpu.interrupt.mask = ipi_target_mask(icr, self);
      packet.guest_vcpu.interrupt.vector = icr.vector;
      next_rip(exit_info, vmcs);
      return ZX_ERR_NEXT;
    }
    case InterruptDeliveryMode::NMI: {
      uint16_t self = vmcs.Read(VmcsField16::VPID) - 1;
      memset(&packet, 0, sizeof(packet));
      packet.type = ZX_PKT_TYPE_GUEST_VCPU;
      packet.guest_vcpu.type = ZX_PKT_GUEST_VCPU_INTERRUPT;
      // Intel Volume 3a, Table 10-4 specifies that NMI to self is an invalid configuration and
      // behavior is undefined for invalid configurations.
      //
      // For simplicity we'll just clear the self-bit in the mask.
      packet.guest_vcpu.interrupt.mask = ipi_target_mask(icr, self) & ~(1 << self);

      // Intel Volume 3a, Section 10.6.1 Interrupt Command Register.
      //
      // For NMI the target information is ignored since the NMI vector is already defined.
      packet.guest_vcpu.interrupt.vector = X86_INT_NMI;
      next_rip(exit_info, vmcs);
      return ZX_ERR_NEXT;
    }
    case InterruptDeliveryMode::INIT:
      // Ignore INIT IPIs, we only need STARTUP to bring up a VCPU.
      next_rip(exit_info, vmcs);
      return ZX_OK;
    case InterruptDeliveryMode::STARTUP:
      memset(&packet, 0, sizeof(packet));
      packet.type = ZX_PKT_TYPE_GUEST_VCPU;
      packet.guest_vcpu.type = ZX_PKT_GUEST_VCPU_STARTUP;
      packet.guest_vcpu.startup.id = icr.destination;
      packet.guest_vcpu.startup.entry = icr.vector << 12;
      next_rip(exit_info, vmcs);
      return ZX_ERR_NEXT;
    default:
      dprintf(INFO, "hypervisor: Unsupported guest IPI delivery mode %#x\n",
              static_cast<uint8_t>(icr.delivery_mode));
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t handle_apic_wrmsr(const ExitInfo& exit_info, AutoVmcs& vmcs,
                              const GuestState& guest_state, LocalApicState& local_apic_state,
                              zx_port_packet& packet) {
  // Check for writes to reserved bits.
  //
  // From Volume 3, Section 10.12.1.2: "The upper 32-bits of all x2APIC MSRs
  // (except for the ICR) are reserved."
  X2ApicMsr reg = static_cast<X2ApicMsr>(guest_state.ecx());
  if (unlikely(guest_state.edx() != 0 && reg != X2ApicMsr::ICR)) {
    local_apic_state.interrupt_tracker.Interrupt(X86_INT_GP_FAULT);
    return ZX_OK;
  }

  switch (reg) {
    case X2ApicMsr::EOI:
    case X2ApicMsr::ESR:
      // From Volume 3, Section 10.12.1.2: "WRMSR of a non-zero value causes #GP(0)."
      if (guest_state.eax() != 0) {
        local_apic_state.interrupt_tracker.Interrupt(X86_INT_GP_FAULT);
        return ZX_OK;
      }
      next_rip(exit_info, vmcs);
      return ZX_OK;
    case X2ApicMsr::TPR:
    case X2ApicMsr::SVR:
    case X2ApicMsr::LVT_MONITOR:
    case X2ApicMsr::LVT_ERROR:
    case X2ApicMsr::LVT_LINT0:
    case X2ApicMsr::LVT_LINT1:
    case X2ApicMsr::LVT_THERMAL_SENSOR:
    case X2ApicMsr::LVT_CMCI:
      next_rip(exit_info, vmcs);
      return ZX_OK;
    case X2ApicMsr::LVT_TIMER:
      if ((guest_state.eax() & LVT_TIMER_MODE_MASK) == LVT_TIMER_MODE_RESERVED) {
        return ZX_ERR_INVALID_ARGS;
      }
      next_rip(exit_info, vmcs);
      local_apic_state.lvt_timer = guest_state.eax();
      update_timer(local_apic_state, lvt_deadline(local_apic_state));
      return ZX_OK;
    case X2ApicMsr::INITIAL_COUNT:
      next_rip(exit_info, vmcs);
      local_apic_state.lvt_initial_count = guest_state.eax();
      update_timer(local_apic_state, lvt_deadline(local_apic_state));
      return ZX_OK;
    case X2ApicMsr::DCR:
      next_rip(exit_info, vmcs);
      local_apic_state.lvt_divide_config = guest_state.eax();
      update_timer(local_apic_state, lvt_deadline(local_apic_state));
      return ZX_OK;
    case X2ApicMsr::SELF_IPI: {
      next_rip(exit_info, vmcs);
      uint32_t vector = guest_state.eax() & UINT8_MAX;
      local_apic_state.interrupt_tracker.Interrupt(vector);
      return ZX_OK;
    }
    case X2ApicMsr::ICR:
      return handle_ipi(exit_info, vmcs, guest_state, packet);
    default:
      // Issue a general protection fault for read only and unimplemented
      // registers.
      dprintf(INFO, "hypervisor: Unhandled guest x2APIC WRMSR %#" PRIx32 "\n", guest_state.ecx());
      local_apic_state.interrupt_tracker.Interrupt(X86_INT_GP_FAULT);
      return ZX_OK;
  }
}

zx_status_t handle_kvm_wrmsr(const ExitInfo& exit_info, AutoVmcs& vmcs,
                             const GuestState& guest_state, LocalApicState& local_apic_state,
                             PvClockState& pv_clock, hypervisor::GuestPhysicalAspace& gpa) {
  zx_paddr_t guest_paddr = guest_state.EdxEax();

  next_rip(exit_info, vmcs);
  switch (guest_state.ecx()) {
    case kKvmSystemTimeMsrOld:
    case kKvmSystemTimeMsr:
      vmcs.Invalidate();
      if ((guest_paddr & 1) != 0) {
        return pv_clock_reset_clock(&pv_clock, &gpa, guest_paddr & ~static_cast<zx_paddr_t>(1))
            .status_value();
      } else {
        pv_clock_stop_clock(&pv_clock);
      }
      return ZX_OK;
    case kKvmBootTimeOld:
    case kKvmBootTime:
      vmcs.Invalidate();
      return pv_clock_update_boot_time(&gpa, guest_paddr).status_value();
    default:
      local_apic_state.interrupt_tracker.Interrupt(X86_INT_GP_FAULT);
      return ZX_OK;
  }
}

zx_status_t handle_wrmsr(const ExitInfo& exit_info, AutoVmcs& vmcs, const GuestState& guest_state,
                         LocalApicState& local_apic_state, PvClockState& pv_clock,
                         hypervisor::GuestPhysicalAspace& gpa, zx_port_packet& packet) {
  // On execution of wrmsr, rcx specifies the MSR and edx:eax contains the value to be written.
  switch (guest_state.ecx()) {
    case X86_MSR_IA32_APIC_BASE:
      if ((guest_state.EdxEax() & ~IA32_APIC_BASE_BSP) != kLocalApicPhysBase) {
        return ZX_ERR_INVALID_ARGS;
      }
      next_rip(exit_info, vmcs);
      return ZX_OK;
    // See note in handle_rdmsr.
    case X86_MSR_IA32_MTRRCAP:
    case X86_MSR_IA32_MTRR_DEF_TYPE:
    case X86_MSR_IA32_MTRR_FIX64K_00000:
    case X86_MSR_IA32_MTRR_FIX16K_80000 ... X86_MSR_IA32_MTRR_FIX16K_A0000:
    case X86_MSR_IA32_MTRR_FIX4K_C0000 ... X86_MSR_IA32_MTRR_FIX4K_F8000:
    case X86_MSR_IA32_MTRR_PHYSBASE0 ... X86_MSR_IA32_MTRR_PHYSMASK9:
    case X86_MSR_IA32_BIOS_SIGN_ID:
    case X86_MSR_DRAM_POWER_LIMIT:
    case X86_MSR_PP0_POWER_LIMIT:
    case X86_MSR_PP1_POWER_LIMIT:
    case X86_MSR_PLATFORM_POWER_LIMIT:
    // We disable the associated CPUID bits, but Linux still writes to these
    // MSRs. Just ignore it.
    case X86_MSR_IA32_SPEC_CTRL:
    case X86_MSR_IA32_PRED_CMD:
    // From AMD64 Volume 2, Section 6.1.1: CSTAR is unused, but Linux likes to
    // set a null handler, even when not in compatibility mode. Just ignore it.
    case X86_MSR_IA32_CSTAR:
      next_rip(exit_info, vmcs);
      return ZX_OK;
    case X86_MSR_IA32_TSC_DEADLINE: {
      if ((local_apic_state.lvt_timer & LVT_TIMER_MODE_MASK) != LVT_TIMER_MODE_TSC_DEADLINE) {
        return ZX_ERR_INVALID_ARGS;
      }
      next_rip(exit_info, vmcs);
      int64_t tsc_deadline = static_cast<int64_t>(guest_state.EdxEax());
      zx_time_t mono_deadline = convert_raw_tsc_timestamp_to_clock_monotonic(tsc_deadline);
      update_timer(local_apic_state, mono_deadline);
      return ZX_OK;
    }
    case kX2ApicMsrBase ... kX2ApicMsrMax:
      return handle_apic_wrmsr(exit_info, vmcs, guest_state, local_apic_state, packet);
    case kKvmSystemTimeMsrOld:
    case kKvmSystemTimeMsr:
    case kKvmBootTimeOld:
    case kKvmBootTime:
      return handle_kvm_wrmsr(exit_info, vmcs, guest_state, local_apic_state, pv_clock, gpa);
    default:
      dprintf(INFO, "hypervisor: Unhandled guest WRMSR %#lx\n", guest_state.rcx);
      local_apic_state.interrupt_tracker.Interrupt(X86_INT_GP_FAULT);
      return ZX_OK;
  }
}

uint8_t default_operand_size(uint64_t efer, uint32_t cs_access_rights) {
  // See Volume 3, Section 5.2.1.
  if ((efer & X86_EFER_LMA) && (cs_access_rights & kGuestXxAccessRightsL)) {
    // IA32-e 64 bit mode.
    return 4;
  } else if (cs_access_rights & kGuestXxAccessRightsD) {
    // CS.D set (and not 64 bit mode).
    return 4;
  } else {
    // CS.D clear (and not 64 bit mode).
    return 2;
  }
}

zx_status_t handle_trap(const ExitInfo& exit_info, AutoVmcs& vmcs, bool read,
                        zx_vaddr_t guest_paddr, hypervisor::TrapMap& traps,
                        zx_port_packet_t& packet) {
  zx::result<hypervisor::Trap*> trap = traps.FindTrap(ZX_GUEST_TRAP_BELL, guest_paddr);
  if (trap.is_error()) {
    return trap.status_value();
  }
  next_rip(exit_info, vmcs);

  switch ((*trap)->kind()) {
    case ZX_GUEST_TRAP_BELL:
      if (read) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      packet.key = (*trap)->key();
      packet.type = ZX_PKT_TYPE_GUEST_BELL;
      packet.guest_bell.addr = guest_paddr;
      if (!(*trap)->HasPort()) {
        return ZX_ERR_BAD_STATE;
      }
      return (*trap)->Queue(packet, &vmcs).status_value();
    case ZX_GUEST_TRAP_MEM:
      if (exit_info.exit_instruction_length > kMaxInstructionSize) {
        return ZX_ERR_INTERNAL;
      }
      packet.key = (*trap)->key();
      packet.type = ZX_PKT_TYPE_GUEST_MEM;
      packet.guest_mem.addr = guest_paddr;
      packet.guest_mem.cr3 = vmcs.Read(VmcsFieldXX::GUEST_CR3);
      packet.guest_mem.rip = exit_info.guest_rip;
      packet.guest_mem.instruction_size = static_cast<uint8_t>(exit_info.exit_instruction_length);
      packet.guest_mem.default_operand_size = default_operand_size(
          vmcs.Read(VmcsField64::GUEST_IA32_EFER), vmcs.Read(VmcsField32::GUEST_CS_ACCESS_RIGHTS));
      return ZX_ERR_NEXT;
    default:
      return ZX_ERR_BAD_STATE;
  }
}

zx_status_t handle_ept_violation(const ExitInfo& exit_info, AutoVmcs& vmcs,
                                 hypervisor::GuestPhysicalAspace& gpa, hypervisor::TrapMap& traps,
                                 zx_port_packet_t& packet) {
  const EptViolationInfo ept_violation_info(vmcs.Read(VmcsFieldXX::EXIT_QUALIFICATION));
  zx_gpaddr_t guest_paddr = vmcs.Read(VmcsField64::GUEST_PHYSICAL_ADDRESS);
  zx_status_t status =
      handle_trap(exit_info, vmcs, ept_violation_info.read, guest_paddr, traps, packet);
  switch (status) {
    case ZX_ERR_NOT_FOUND:
      break;
    case ZX_OK:
    default:
      return status;
  }
  // We may have to block when handling the page fault.
  vmcs.Invalidate();

  // If there was no trap associated with this address and it is outside of
  // guest physical address space, return failure.
  if (guest_paddr >= gpa.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (auto result = gpa.PageFault(guest_paddr); result.is_error()) {
    dprintf(CRITICAL, "hypervisor: Unhandled EPT violation %#lx\n", guest_paddr);
    return result.status_value();
  }
  return ZX_OK;
}

zx_status_t handle_xsetbv(const ExitInfo& exit_info, AutoVmcs& vmcs, GuestState& guest_state) {
  uint64_t guest_cr4 = vmcs.Read(VmcsFieldXX::GUEST_CR4);
  if (!(guest_cr4 & X86_CR4_OSXSAVE)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // We only support XCR0.
  if (guest_state.rcx != 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  cpuid_leaf leaf;
  if (!x86_get_cpuid_subleaf(X86_CPUID_XSAVE, 0, &leaf)) {
    return ZX_ERR_INTERNAL;
  }

  // Check that XCR0 is valid.
  uint64_t xcr0_bitmap = (static_cast<uint64_t>(leaf.d) << 32) | leaf.a;
  uint64_t xcr0 = guest_state.EdxEax();
  if (~xcr0_bitmap & xcr0 ||
      // x87 state must be enabled.
      (xcr0 & X86_XSAVE_STATE_BIT_X87) != X86_XSAVE_STATE_BIT_X87 ||
      // If AVX state is enabled, SSE state must be enabled.
      (xcr0 & (X86_XSAVE_STATE_BIT_AVX | X86_XSAVE_STATE_BIT_SSE)) == X86_XSAVE_STATE_BIT_AVX) {
    return ZX_ERR_INVALID_ARGS;
  }

  guest_state.xcr0 = xcr0;
  next_rip(exit_info, vmcs);
  return ZX_OK;
}

zx_status_t handle_pause(const ExitInfo& exit_info, AutoVmcs& vmcs) {
  next_rip(exit_info, vmcs);
  return ZX_OK;
}

bool is_cpl0(AutoVmcs& vmcs, GuestState& guest_state) {
  const uint32_t access_rights = vmcs.Read(VmcsField32::GUEST_SS_ACCESS_RIGHTS);
  // We only accept a VMCALL if CPL is 0.
  return (access_rights & kGuestXxAccessRightsDplUser) == 0;
}

zx_status_t handle_vmcall_regular(const ExitInfo& exit_info, AutoVmcs& vmcs,
                                  GuestState& guest_state, hypervisor::GuestPhysicalAspace& gpa) {
  next_rip(exit_info, vmcs);
  if (!is_cpl0(vmcs, guest_state)) {
    guest_state.rax = VmCallStatus::NOT_PERMITTED;
    return ZX_OK;
  }
  vmcs.Invalidate();
  const VmCallInfo info(guest_state);
  switch (info.type) {
    case VmCallType::CLOCK_PAIRING: {
      if (info.arg[1] != 0) {
        dprintf(INFO, "hypervisor: CLOCK_PAIRING hypercall doesn't support clock type %lu\n",
                info.arg[1]);
        guest_state.rax = VmCallStatus::NOT_SUPPORTED;
        break;
      }
      if (auto result = pv_clock_populate_offset(&gpa, info.arg[0]); result.is_error()) {
        dprintf(INFO, "hypervisor: Failed to populate lock offset with error %d\n",
                result.status_value());
        guest_state.rax = VmCallStatus::FAULT;
        break;
      }
      guest_state.rax = VmCallStatus::OK;
      break;
    }
    default:
      dprintf(INFO,
              "hypervisor: Unknown hypercall %lu (arg0=%#lx, arg1=%#lx, arg2=%#lx, arg3=%#lx)\n",
              static_cast<uint64_t>(info.type), info.arg[0], info.arg[1], info.arg[2], info.arg[3]);
      guest_state.rax = VmCallStatus::UNKNOWN_HYPERCALL;
      break;
  }
  // We never fail in case of hypercalls, we just return/propagate errors to the caller.
  return ZX_OK;
}

zx_status_t handle_vmcall_direct(const ExitInfo& exit_info, AutoVmcs& vmcs, GuestState& guest_state,
                                 uintptr_t& fs_base, zx_port_packet_t& packet) {
  next_rip(exit_info, vmcs);
  if (!is_cpl0(vmcs, guest_state)) {
    guest_state.rax = ZX_ERR_ACCESS_DENIED;
    return ZX_OK;
  }
  vmcs.Invalidate();
  return vmcall_dispatch(guest_state, fs_base, packet);
}

}  // namespace

ExitInfo::ExitInfo(const AutoVmcs& vmcs) {
  // From Volume 3, Section 26.7.
  uint32_t full_exit_reason = vmcs.Read(VmcsField32::EXIT_REASON);
  entry_failure = BIT(full_exit_reason, 31);
  exit_reason = static_cast<ExitReason>(BITS(full_exit_reason, 15, 0));

  exit_instruction_length = vmcs.Read(VmcsField32::EXIT_INSTRUCTION_LENGTH);
  guest_rip = vmcs.Read(VmcsFieldXX::GUEST_RIP);

  if (exit_reason == ExitReason::EXTERNAL_INTERRUPT || exit_reason == ExitReason::IO_INSTRUCTION) {
    return;
  }

  LTRACEF("entry failure: %d\n", entry_failure);
  LTRACEF("exit reason: %#x (%s)\n", static_cast<uint32_t>(exit_reason),
          exit_reason_name(exit_reason));
  LTRACEF("exit instruction length: %#x\n", exit_instruction_length);
  LTRACEF("guest activity state: %#x\n", vmcs.Read(VmcsField32::GUEST_ACTIVITY_STATE));
  LTRACEF("guest interruptibility state: %#x\n",
          vmcs.Read(VmcsField32::GUEST_INTERRUPTIBILITY_STATE));
  LTRACEF("guest linear address: %#lx\n", vmcs.Read(VmcsFieldXX::GUEST_LINEAR_ADDRESS));
  LTRACEF("guest rip: %#lx\n", guest_rip);
}

ExitInterruptionInfo::ExitInterruptionInfo(const AutoVmcs& vmcs) {
  uint32_t int_info = vmcs.Read(VmcsField32::EXIT_INTERRUPTION_INFORMATION);
  vector = static_cast<uint8_t>(BITS(int_info, 7, 0));
  interruption_type = static_cast<InterruptionType>(BITS_SHIFT(int_info, 10, 8));
  error_code_valid = BIT(int_info, 11);
  valid = BIT(int_info, 31);
}

PageFaultInfo::PageFaultInfo(uint32_t error_code) {
  // From Volume 3A, Figure 4-12.
  flags = 0;
  flags |= (error_code & PFEX_W) ? VMM_PF_FLAG_WRITE : 0;
  flags |= (error_code & PFEX_U) ? VMM_PF_FLAG_USER : 0;
  flags |= (error_code & PFEX_I) ? VMM_PF_FLAG_INSTRUCTION : 0;
  flags |= (error_code & PFEX_P) ? 0 : VMM_PF_FLAG_NOT_PRESENT;
}

EptViolationInfo::EptViolationInfo(uint64_t qualification) {
  // From Volume 3C, Table 27-7.
  read = BIT(qualification, 0);
  write = BIT(qualification, 1);
  instruction = BIT(qualification, 2);
}

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

InterruptCommandRegister::InterruptCommandRegister(uint32_t hi, uint32_t lo) {
  destination = hi;
  destination_mode = static_cast<InterruptDestinationMode>(BIT_SHIFT(lo, 11));
  delivery_mode = static_cast<InterruptDeliveryMode>(BITS_SHIFT(lo, 10, 8));
  destination_shorthand = static_cast<InterruptDestinationShorthand>(BITS_SHIFT(lo, 19, 18));
  vector = static_cast<uint8_t>(BITS(lo, 7, 0));
}

VmCallInfo::VmCallInfo(const GuestState& guest_state) {
  // ABI is documented in Linux kernel documentation, see
  // Documents/virtual/kvm/hypercalls.txt
  type = static_cast<VmCallType>(guest_state.rax);
  arg[0] = guest_state.rbx;
  arg[1] = guest_state.rcx;
  arg[2] = guest_state.rdx;
  arg[3] = guest_state.rsi;
}

zx_status_t vmexit_handler_normal(AutoVmcs& vmcs, GuestState& guest_state,
                                  LocalApicState& local_apic_state, PvClockState& pv_clock,
                                  hypervisor::GuestPhysicalAspace& gpa, hypervisor::TrapMap& traps,
                                  zx_port_packet_t& packet) {
  zx_status_t status;
  const ExitInfo exit_info(vmcs);
  switch (exit_info.exit_reason) {
    case ExitReason::EXTERNAL_INTERRUPT:
      ktrace_vcpu_exit(VCPU_EXTERNAL_INTERRUPT, exit_info.guest_rip);
      GUEST_STATS_INC(interrupts);
      status = handle_external_interrupt(vmcs);
      break;
    case ExitReason::INTERRUPT_WINDOW:
      ktrace_vcpu_exit(VCPU_INTERRUPT_WINDOW, exit_info.guest_rip);
      GUEST_STATS_INC(interrupt_windows);
      status = handle_interrupt_window(vmcs);
      break;
    case ExitReason::CPUID:
      ktrace_vcpu_exit(VCPU_CPUID, exit_info.guest_rip);
      GUEST_STATS_INC(cpuid_instructions);
      status = handle_cpuid(exit_info, vmcs, guest_state);
      break;
    case ExitReason::HLT:
      ktrace_vcpu_exit(VCPU_HLT, exit_info.guest_rip);
      GUEST_STATS_INC(hlt_instructions);
      status = handle_hlt(exit_info, vmcs, local_apic_state);
      break;
    case ExitReason::CONTROL_REGISTER_ACCESS:
      ktrace_vcpu_exit(VCPU_CONTROL_REGISTER_ACCESS, exit_info.guest_rip);
      GUEST_STATS_INC(control_register_accesses);
      status = handle_control_register_access(exit_info, vmcs, guest_state, local_apic_state);
      break;
    case ExitReason::IO_INSTRUCTION:
      ktrace_vcpu_exit(VCPU_IO_INSTRUCTION, exit_info.guest_rip);
      GUEST_STATS_INC(io_instructions);
      status = handle_io_instruction(exit_info, vmcs, guest_state, traps, packet);
      break;
    case ExitReason::RDMSR:
      ktrace_vcpu_exit(VCPU_RDMSR, exit_info.guest_rip);
      GUEST_STATS_INC(rdmsr_instructions);
      status = handle_rdmsr(exit_info, vmcs, guest_state, local_apic_state);
      break;
    case ExitReason::WRMSR:
      ktrace_vcpu_exit(VCPU_WRMSR, exit_info.guest_rip);
      GUEST_STATS_INC(wrmsr_instructions);
      status = handle_wrmsr(exit_info, vmcs, guest_state, local_apic_state, pv_clock, gpa, packet);
      break;
    case ExitReason::ENTRY_FAILURE_GUEST_STATE:
    case ExitReason::ENTRY_FAILURE_MSR_LOADING:
    case ExitReason::ENTRY_FAILURE_MACHINE_CHECK:
      ktrace_vcpu_exit(VCPU_VM_ENTRY_FAILURE, exit_info.guest_rip);
      status = ZX_ERR_BAD_STATE;
      break;
    case ExitReason::EPT_VIOLATION:
      ktrace_vcpu_exit(VCPU_EPT_VIOLATION, exit_info.guest_rip);
      GUEST_STATS_INC(ept_violations);
      status = handle_ept_violation(exit_info, vmcs, gpa, traps, packet);
      break;
    case ExitReason::XSETBV:
      ktrace_vcpu_exit(VCPU_XSETBV, exit_info.guest_rip);
      GUEST_STATS_INC(xsetbv_instructions);
      status = handle_xsetbv(exit_info, vmcs, guest_state);
      break;
    case ExitReason::PAUSE:
      ktrace_vcpu_exit(VCPU_PAUSE, exit_info.guest_rip);
      GUEST_STATS_INC(pause_instructions);
      status = handle_pause(exit_info, vmcs);
      break;
    case ExitReason::VMCALL:
      ktrace_vcpu_exit(VCPU_VMCALL, exit_info.guest_rip);
      GUEST_STATS_INC(vmcall_instructions);
      status = handle_vmcall_regular(exit_info, vmcs, guest_state, gpa);
      break;
    case ExitReason::EXCEPTION_OR_NMI:
    // Currently all exceptions, except NMIs, are delivered directly to guests.
    // NMIs cause VM exits and are handled by the host via the IDT as any other
    // interrupt/exception.
    default:
      ktrace_vcpu_exit(VCPU_NOT_SUPPORTED, exit_info.guest_rip);
      status = ZX_ERR_NOT_SUPPORTED;
      break;
  }
  switch (status) {
    case ZX_OK:
    case ZX_ERR_NEXT:
    case ZX_ERR_INTERNAL_INTR_RETRY:
    case ZX_ERR_INTERNAL_INTR_KILLED:
      break;
    default:
      dprintf(CRITICAL, "hypervisor: VM exit handler (regular) for %s (%u) returned %d\n",
              exit_reason_name(exit_info.exit_reason), static_cast<uint32_t>(exit_info.exit_reason),
              status);
      dump_guest_state(guest_state, exit_info);
      break;
  }
  return status;
}

zx_status_t vmexit_handler_direct(AutoVmcs& vmcs, GuestState& guest_state, VmAspace& user_aspace,
                                  uintptr_t& fs_base, zx_port_packet_t& packet) {
  zx_status_t status;
  const ExitInfo exit_info(vmcs);
  switch (exit_info.exit_reason) {
    case ExitReason::EXCEPTION_OR_NMI:
      ktrace_vcpu_exit(VCPU_EXCEPTION_OR_NMI, exit_info.guest_rip);
      status = handle_exception_or_nmi(vmcs, user_aspace);
      break;
    case ExitReason::EXTERNAL_INTERRUPT:
      ktrace_vcpu_exit(VCPU_EXTERNAL_INTERRUPT, exit_info.guest_rip);
      GUEST_STATS_INC(interrupts);
      status = handle_external_interrupt(vmcs);
      break;
    case ExitReason::CPUID:
      ktrace_vcpu_exit(VCPU_CPUID, exit_info.guest_rip);
      GUEST_STATS_INC(cpuid_instructions);
      status = handle_cpuid(exit_info, vmcs, guest_state);
      break;
    case ExitReason::VMCALL:
      ktrace_vcpu_exit(VCPU_VMCALL, exit_info.guest_rip);
      GUEST_STATS_INC(vmcall_instructions);
      status = handle_vmcall_direct(exit_info, vmcs, guest_state, fs_base, packet);
      break;
    case ExitReason::ENTRY_FAILURE_GUEST_STATE:
    case ExitReason::ENTRY_FAILURE_MSR_LOADING:
    case ExitReason::ENTRY_FAILURE_MACHINE_CHECK:
      ktrace_vcpu_exit(VCPU_VM_ENTRY_FAILURE, exit_info.guest_rip);
      status = ZX_ERR_BAD_STATE;
      break;
    default:
      ktrace_vcpu_exit(VCPU_NOT_SUPPORTED, exit_info.guest_rip);
      status = ZX_ERR_NOT_SUPPORTED;
      break;
  }
  switch (status) {
    case ZX_OK:
    case ZX_ERR_NEXT:
    case ZX_ERR_INTERNAL_INTR_RETRY:
    case ZX_ERR_INTERNAL_INTR_KILLED:
      break;
    default:
      dprintf(CRITICAL, "hypervisor: VM exit handler (direct) for %s (%u) returned %d\n",
              exit_reason_name(exit_info.exit_reason), static_cast<uint32_t>(exit_info.exit_reason),
              status);
      dump_guest_state(guest_state, exit_info);
      break;
  }
  return status;
}
