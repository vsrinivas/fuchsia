// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <hypervisor/ktrace.h>
#include <kernel/thread.h>
#include <lib/ktrace.h>

static const char* const vcpu_meta[] = {
#if ARCH_ARM64
    [VCPU_PHYSICAL_INTERRUPT] = "exit:physical-interrupt",
    [VCPU_WFI_WFE_INSTRUCTION] = "exit:wfi_wfe_instruction",
    [VCPU_SMC_INSTRUCTION] = "exit:smc_instruction",
    [VCPU_SYSTEM_INSTRUCTION] = "exit:system_instruction",
    [VCPU_INSTRUCTION_ABORT] = "exit:instruction_abort",
    [VCPU_DATA_ABORT] = "exit:data_abort",
#elif ARCH_X86
    [VCPU_EXTERNAL_INTERRUPT] = "exit:external_interrupt",
    [VCPU_INTERRUPT_WINDOW] = "exit:interrupt_window",
    [VCPU_CPUID] = "exit:cpuid",
    [VCPU_HLT] = "exit:hlt",
    [VCPU_CONTROL_REGISTER_ACCESS] = "exit:control_register_access",
    [VCPU_IO_INSTRUCTION] = "exit:io_instruction",
    [VCPU_RDMSR] = "exit:rdmsr",
    [VCPU_WRMSR] = "exit:wrmsr",
    [VCPU_VM_ENTRY_FAILURE] = "exit:vm_entry_failure",
    [VCPU_EPT_VIOLATION] = "exit:ept_violation",
    [VCPU_XSETBV] = "exit:xsetbv",
    [VCPU_PAUSE] = "exit:pause",
#endif
    [VCPU_UNKNOWN] = "exit:unknown",
    [VCPU_FAILURE] = "exit:failure",

    [VCPU_INTERRUPT] = "wait:interrupt",
    [VCPU_PORT] = "wait:port",
};
static_assert((sizeof(vcpu_meta) / sizeof(vcpu_meta[0])) == VCPU_META_COUNT,
              "vcpu_meta array must match enum VcpuMeta");

void ktrace_report_vcpu_meta() {
  for (uint32_t i = 0; i != VCPU_META_COUNT; i++) {
    ktrace_name_etc(TAG_VCPU_META, i, 0, vcpu_meta[i], true);
  }
}

void ktrace_vcpu(uint32_t tag, VcpuMeta meta) {
  ktrace(tag, meta, 0, 0, 0);
}
