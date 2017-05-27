// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <kernel/thread.h>
#include <lib/ktrace.h>
#include <lib/vdso.h>
#include <magenta/mx-syscall-numbers.h>
#include <magenta/process_dispatcher.h>
#include <platform.h>
#include <trace.h>

#include <inttypes.h>
#include <stdint.h>

#include "syscalls_priv.h"
#include "vdso-valid-sysret.h"

#define LOCAL_TRACE 0

int sys_invalid_syscall(uint64_t num, uint64_t pc,
                        uintptr_t vdso_code_address) {
    LTRACEF("invalid syscall %lu from PC %#lx vDSO code %#lx\n",
            num, pc, vdso_code_address);
    return ERR_BAD_SYSCALL;
}

inline uint64_t invoke_syscall(
    uint64_t syscall_num, uint64_t pc,
    uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4,
    uint64_t arg5, uint64_t arg6, uint64_t arg7, uint64_t arg8) {
    uint64_t ret;

    const uintptr_t vdso_code_address =
        ProcessDispatcher::GetCurrent()->vdso_code_address();
    const uint64_t pc_offset = pc - vdso_code_address;

#define CHECK_SYSCALL_PC(name)                                          \
    do {                                                                \
        if (unlikely(!VDso::ValidSyscallPC::name(pc_offset)))           \
            return sys_invalid_syscall(syscall_num, pc, vdso_code_address); \
    } while (0)

    switch (syscall_num) {
#include <magenta/syscall-invocation-cases.inc>
    default:
        // This should be unreachable because the numbers are densely packed.
        ASSERT_MSG(
            0, "invalid syscall number %lu from PC %#lx reached switch!",
            syscall_num, pc);
    }

    return ret;
}

#if ARCH_ARM64
#include <arch/arm64.h>

// N.B. Interrupts must be disabled on entry and they will be disabled on exit.
// The reason is the two calls two arch_curr_cpu_num in the ktrace calls: we
// don't want the cpu changing during the call.

extern "C" void arm64_syscall(struct arm64_iframe_long* frame, bool is_64bit, uint64_t pc) {
    uint64_t syscall_num = frame->r[16];

    ktrace_tiny(TAG_SYSCALL_ENTER, ((uint32_t)syscall_num << 8) | arch_curr_cpu_num());

    THREAD_STATS_INC(syscalls);

    /* re-enable interrupts to maintain kernel preemptiveness
       This must be done after the above ktrace_tiny call, and after the
       above THREAD_STATS_INC call as it also calls arch_curr_cpu_num. */
    arch_enable_ints();

    LTRACEF_LEVEL(2, "num %" PRIu64 "\n", syscall_num);

    /* call the routine */
    uint64_t ret = invoke_syscall(
        syscall_num, pc,
        frame->r[0], frame->r[1], frame->r[2], frame->r[3],
        frame->r[4], frame->r[5], frame->r[6], frame->r[7]);

    LTRACEF_LEVEL(2, "ret %#" PRIx64 "\n", ret);

    /* put the return code back */
    frame->r[0] = ret;

    /* re-disable interrupts on the way out
       This must be done before the below ktrace_tiny call. */
    arch_disable_ints();

    ktrace_tiny(TAG_SYSCALL_EXIT, ((uint32_t)syscall_num << 8) | arch_curr_cpu_num());
}

#endif

#if ARCH_X86_64
#include <arch/x86.h>

// N.B. Interrupts must be disabled on entry and they will be disabled on exit.
// The reason is the two calls two arch_curr_cpu_num in the ktrace calls: we
// don't want the cpu changing during the call.

template <typename T>
inline x86_64_syscall_result do_syscall(uint64_t syscall_num, uint64_t ip,
                                        bool (*valid_pc)(uintptr_t), T make_call) {
    ktrace_tiny(TAG_SYSCALL_ENTER, (static_cast<uint32_t>(syscall_num) << 8) | arch_curr_cpu_num());

    THREAD_STATS_INC(syscalls);

    /* re-enable interrupts to maintain kernel preemptiveness
       This must be done after the above ktrace_tiny call, and after the
       above THREAD_STATS_INC call as it also calls arch_curr_cpu_num. */
    arch_enable_ints();

    LTRACEF_LEVEL(2, "t %p syscall num %" PRIu64 " ip %#" PRIx64 "\n",
                  get_current_thread(), syscall_num, ip);

    const uintptr_t vdso_code_address =
        ProcessDispatcher::GetCurrent()->vdso_code_address();

    uint64_t ret;
    if (unlikely(!valid_pc(ip - vdso_code_address))) {
        ret = sys_invalid_syscall(syscall_num, ip, vdso_code_address);
    } else {
        ret = make_call();
    }

    LTRACEF_LEVEL(2, "t %p ret %#" PRIx64 "\n", get_current_thread(), ret);

    /* re-disable interrupts on the way out
       This must be done before the below ktrace_tiny call. */
    arch_disable_ints();

    ktrace_tiny(TAG_SYSCALL_EXIT, (static_cast<uint32_t>(syscall_num << 8)) | arch_curr_cpu_num());

    // The assembler caller will re-disable interrupts at the appropriate time.
    return {ret, thread_is_signaled(get_current_thread())};
}

inline x86_64_syscall_result unknown_syscall(uint64_t syscall_num, uint64_t ip) {
    return do_syscall(syscall_num, ip,
                      [](uintptr_t) { return false; },
                      [&]() {
                          __builtin_unreachable();
                          return ERR_INTERNAL;
                      });
}

#endif

// Autogenerated per-syscall wrapper functions.
#include <magenta/syscall-kernel-wrappers.inc>
