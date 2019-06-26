// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_KTRACE_H_
#define ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_KTRACE_H_

#include <stdint.h>

enum VcpuMeta : uint32_t {
  // Waits.
  VCPU_INTERRUPT,
  VCPU_PORT,

  // Do not use.
  VCPU_META_COUNT,
};

enum VcpuExit : uint32_t {
#if ARCH_ARM64
  VCPU_UNDERFLOW_MAINTENANCE_INTERRUPT,
  VCPU_PHYSICAL_INTERRUPT,
  VCPU_WFI_INSTRUCTION,
  VCPU_WFE_INSTRUCTION,
  VCPU_SMC_INSTRUCTION,
  VCPU_SYSTEM_INSTRUCTION,
  VCPU_INSTRUCTION_ABORT,
  VCPU_DATA_ABORT,
#elif ARCH_X86
  VCPU_EXTERNAL_INTERRUPT,
  VCPU_INTERRUPT_WINDOW,
  VCPU_CPUID,
  VCPU_HLT,
  VCPU_CONTROL_REGISTER_ACCESS,
  VCPU_IO_INSTRUCTION,
  VCPU_RDMSR,
  VCPU_WRMSR,
  VCPU_VM_ENTRY_FAILURE,
  VCPU_EPT_VIOLATION,
  VCPU_XSETBV,
  VCPU_PAUSE,
  VCPU_VMCALL,
#endif
  VCPU_UNKNOWN,
  VCPU_FAILURE,

  // Do not use.
  VCPU_EXIT_COUNT,
};

void ktrace_report_vcpu_meta();
void ktrace_vcpu(uint32_t tag, VcpuMeta meta);
void ktrace_vcpu_exit(VcpuExit exit, uint64_t exit_address);

#endif  // ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_KTRACE_H_
