// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/ktrace.h>

#include <hypervisor/ktrace.h>
#include <kernel/thread.h>

#if defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc99-designator"
#endif
static StringRef* const vcpu_meta[] = {
    [VCPU_INTERRUPT] = "wait:interrupt"_stringref,
    [VCPU_PORT] = "wait:port"_stringref,
};
static_assert((sizeof(vcpu_meta) / sizeof(vcpu_meta[0])) == VCPU_META_COUNT,
              "vcpu_meta array must match enum VcpuMeta");

static StringRef* const vcpu_exit[] = {
#if ARCH_ARM64
    [VCPU_UNDERFLOW_MAINTENANCE_INTERRUPT] = "exit:underflow_maintenance_interrupt"_stringref,
    [VCPU_PHYSICAL_INTERRUPT] = "exit:physical_interrupt"_stringref,
    [VCPU_WFI_INSTRUCTION] = "exit:wfi_instruction"_stringref,
    [VCPU_WFE_INSTRUCTION] = "exit:wfe_instruction"_stringref,
    [VCPU_SMC_INSTRUCTION] = "exit:smc_instruction"_stringref,
    [VCPU_SYSTEM_INSTRUCTION] = "exit:system_instruction"_stringref,
    [VCPU_INSTRUCTION_ABORT] = "exit:instruction_abort"_stringref,
    [VCPU_DATA_ABORT] = "exit:data_abort"_stringref,
    [VCPU_SERROR_INTERRUPT] = "exit:serror_interrupt"_stringref,
#elif ARCH_X86
    [VCPU_EXCEPTION_OR_NMI] = "exit:exception_or_nmi"_stringref,
    [VCPU_EXTERNAL_INTERRUPT] = "exit:external_interrupt"_stringref,
    [VCPU_INTERRUPT_WINDOW] = "exit:interrupt_window"_stringref,
    [VCPU_CPUID] = "exit:cpuid"_stringref,
    [VCPU_HLT] = "exit:hlt"_stringref,
    [VCPU_CONTROL_REGISTER_ACCESS] = "exit:control_register_access"_stringref,
    [VCPU_IO_INSTRUCTION] = "exit:io_instruction"_stringref,
    [VCPU_RDMSR] = "exit:rdmsr"_stringref,
    [VCPU_WRMSR] = "exit:wrmsr"_stringref,
    [VCPU_VM_ENTRY_FAILURE] = "exit:vm_entry_failure"_stringref,
    [VCPU_EPT_VIOLATION] = "exit:ept_violation"_stringref,
    [VCPU_XSETBV] = "exit:xsetbv"_stringref,
    [VCPU_PAUSE] = "exit:pause"_stringref,
    [VCPU_VMCALL] = "exit:vmcall"_stringref,
#endif
    [VCPU_NOT_SUPPORTED] = "exit:not_supported"_stringref,
    [VCPU_FAILURE] = "exit:failure"_stringref,
};
#if defined(__clang__)
#pragma GCC diagnostic pop
#endif
static_assert((sizeof(vcpu_exit) / sizeof(vcpu_exit[0])) == VCPU_EXIT_COUNT,
              "vcpu_exit array must match enum VcpuExit");

void ktrace_vcpu(uint32_t tag, VcpuMeta meta) {
  if (unlikely(ktrace_tag_enabled(tag))) {
    const Thread* current_thread = Thread::Current::Get();
    const fxt::ThreadRef thread{current_thread->pid(), current_thread->tid()};
    const fxt::StringRef category{"kernel:vcpu"_stringref->GetFxtId()};
    const fxt::Argument arg{fxt::StringRef{"meta #"_stringref->GetFxtId()}, meta};
    const auto name = meta < VCPU_META_COUNT ? fxt::StringRef(vcpu_meta[meta]->GetFxtId())
                                             : fxt::StringRef("vcpu meta"_stringref->GetFxtId());
    if (tag == TAG_VCPU_BLOCK) {
      fxt_duration_begin(tag, current_ticks(), thread, category, name, arg);
    } else if (tag == TAG_VCPU_UNBLOCK) {
      fxt_duration_end(tag, current_ticks(), thread, category, name, arg);
    }
  }
}

void ktrace_vcpu_exit(VcpuExit exit, uint64_t exit_address) {
  if (unlikely(ktrace_tag_enabled(TAG_VCPU_EXIT))) {
    const Thread* current_thread = Thread::Current::Get();
    const fxt::ThreadRef thread{current_thread->pid(), current_thread->tid()};
    const fxt::StringRef category{"kernel:vcpu"_stringref->GetFxtId()};
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId> addr_arg{
        fxt::StringRef{"exit_address"_stringref->GetFxtId()}, exit_address};
    const fxt::StringRef name{"vcpu"_stringref->GetFxtId()};

    if (exit < VCPU_EXIT_COUNT) {
      const fxt::Argument exit_type_arg{fxt::StringRef{"exit_address"_stringref->GetFxtId()},
                                        fxt::StringRef{vcpu_exit[exit]->GetFxtId()}};
      fxt_duration_end(TAG_VCPU_EXIT, current_ticks(), thread, category, name, addr_arg,
                       exit_type_arg);
    } else {
      const fxt::Argument exit_type_arg{fxt::StringRef("exit_address"_stringref->GetFxtId()), exit};
      fxt_duration_end(TAG_VCPU_EXIT, current_ticks(), thread, category, name, addr_arg,
                       exit_type_arg);
    }
  }
}
