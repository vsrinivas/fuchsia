// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

enum VcpuMeta : uint32_t {
    // Exits.
#if ARCH_ARM64
    VCPU_PHYSICAL_INTERRUPT,
    VCPU_WFI_WFE_INSTRUCTION,
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
#endif
    VCPU_UNKNOWN,
    VCPU_FAILURE,

    // Waits.
    VCPU_INTERRUPT,
    VCPU_PORT,

    // Do not use.
    VCPU_META_COUNT,
};

void ktrace_report_vcpu_meta();
void ktrace_vcpu(uint32_t tag, VcpuMeta meta);
