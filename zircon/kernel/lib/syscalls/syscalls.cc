// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/ktrace.h>
#include <lib/syscalls/forward.h>
#include <lib/syscalls/safe-syscall-argument.h>
#include <lib/syscalls/zx-syscall-numbers.h>
#include <lib/userabi/vdso.h>
#include <platform.h>
#include <stdint.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <kernel/stats.h>
#include <kernel/thread.h>
#include <object/process_dispatcher.h>
#include <syscalls/syscalls.h>

#include "vdso-valid-sysret.h"

#define LOCAL_TRACE 0

// Main syscall dispatch routine. For every syscall in the system stamp out a separate
// wrapper_<name of syscall> routine using the do_syscall inline function instantiated
// from an a header generated from an external tool.
//
// The end result is a wrapper_<syscall> that does per syscall argument validation and
// argument marshalling to an inner routine called sys_<syscall>.

namespace {

#define VDSO_SYSCALL(...)
#define KERNEL_SYSCALL(name, type, attrs, nargs, arglist, prototype) \
  [ZX_SYS_##name] = #name##_stringref,
#define INTERNAL_SYSCALL(...) KERNEL_SYSCALL(__VA_ARGS__)
#define BLOCKING_SYSCALL(...) KERNEL_SYSCALL(__VA_ARGS__)

#if defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc99-designator"
#endif
StringRef* const kSyscallNames[] = {
#include <lib/syscalls/kernel.inc>
};
#if defined(__clang__)
#pragma GCC diagnostic pop
#endif

__NO_INLINE int sys_invalid_syscall(uint64_t num, uint64_t pc, uintptr_t vdso_code_address) {
  LTRACEF("invalid syscall %lu from PC %#lx vDSO code %#lx\n", num, pc, vdso_code_address);
  Thread::Current::SignalPolicyException(ZX_EXCP_POLICY_CODE_BAD_SYSCALL,
                                         static_cast<uint32_t>(num));
  return ZX_ERR_BAD_SYSCALL;
}

struct syscall_pre_out {
  uintptr_t vdso_code_address;
  ProcessDispatcher* current_process;
};

fxt::StringRef<fxt::RefType::kId> syscall_name_ref(uint64_t syscall_num) {
  if (syscall_num < ktl::size(kSyscallNames) && kSyscallNames[syscall_num] != nullptr) {
    return fxt::StringRef(kSyscallNames[syscall_num]->GetFxtId());
  } else {
    return fxt::StringRef("Unknown Syscall"_stringref->GetFxtId());
  }
}

// N.B. Interrupts must be disabled on entry and they will be disabled on exit.
// The reason is the two calls two arch_curr_cpu_num in the ktrace calls: we
// don't want the cpu changing during the call.

// Try to do as much as possible in the shared preamble code to maximize code reuse
// between syscalls.
__NO_INLINE syscall_pre_out do_syscall_pre(uint64_t syscall_num, uint64_t pc) {
  if (unlikely(ktrace_tag_enabled(TAG_SYSCALL_ENTER))) {
    Thread* current_thread = Thread::Current::Get();
    fxt_duration_begin(TAG_SYSCALL_ENTER, current_ticks(),
                       fxt::ThreadRef(current_thread->pid(), current_thread->tid()),
                       fxt::StringRef("kernel:syscall"_stringref->GetFxtId()),
                       syscall_name_ref(syscall_num));
  }

  CPU_STATS_INC(syscalls);

  /* re-enable interrupts to maintain kernel preemptiveness
     This must be done after the above fxt_duration_begin call, and after the
     above CPU_STATS_INC call as it also calls arch_curr_cpu_num. */
  arch_enable_ints();

  LTRACEF_LEVEL(2, "t %p syscall num %" PRIu64 " ip/pc %#" PRIx64 "\n", Thread::Current::Get(),
                syscall_num, pc);

  ProcessDispatcher* current_process = ProcessDispatcher::GetCurrent();
  uintptr_t vdso_code_address = current_process->vdso_code_address();

  return {vdso_code_address, current_process};
}

__NO_INLINE syscall_result do_syscall_post(uint64_t ret, uint64_t syscall_num) {
  LTRACEF_LEVEL(2, "t %p ret %#" PRIx64 "\n", Thread::Current::Get(), ret);

  /* re-disable interrupts on the way out
     This must be done before the below fxt_duration_end call. */
  arch_disable_ints();

  if (unlikely(ktrace_tag_enabled(TAG_SYSCALL_EXIT))) {
    Thread* current_thread = Thread::Current::Get();
    fxt_duration_end(TAG_SYSCALL_EXIT, current_ticks(),
                     fxt::ThreadRef(current_thread->pid(), current_thread->tid()),
                     fxt::StringRef("kernel:syscall"_stringref->GetFxtId()),
                     syscall_name_ref(syscall_num));
  }

  // The assembler caller will re-disable interrupts at the appropriate time.
  return {ret, Thread::Current::Get()->IsSignaled()};
}

}  // namespace

// Stamped out syscall veneer routine for every syscall. Try to maximize shared code by forcing
// most of the setup and teardown code into non-inlined preamble and postamble code.
template <typename T>
inline syscall_result do_syscall(uint64_t syscall_num, uint64_t pc, bool (*valid_pc)(uintptr_t),
                                 T make_call) {
  // Call the shared preamble code
  auto pre_ret = do_syscall_pre(syscall_num, pc);
  const uintptr_t vdso_code_address = pre_ret.vdso_code_address;
  ProcessDispatcher* current_process = pre_ret.current_process;

  // Validate the user space program counter originated from the vdso at the proper location,
  // otherwise call through to the invalid syscall handler
  uint64_t ret;
  if (unlikely(!valid_pc(pc - vdso_code_address))) {
    ret = sys_invalid_syscall(syscall_num, pc, vdso_code_address);
  } else {
    // Per syscall inlined routine to marshall args appropriately
    ret = make_call(current_process);
  }

  // Call through to the shared postamble code
  return do_syscall_post(ret, syscall_num);
}

// Called when an out of bounds syscall number is passed from user space
syscall_result unknown_syscall(uint64_t syscall_num, uint64_t pc) {
  return do_syscall(
      syscall_num, pc, [](uintptr_t) { return false; },
      [&](ProcessDispatcher*) {
        __builtin_unreachable();
        return ZX_ERR_INTERNAL;
      });
}

// Autogenerated per-syscall wrapper functions.
#include <lib/syscalls/kernel-wrappers.inc>
