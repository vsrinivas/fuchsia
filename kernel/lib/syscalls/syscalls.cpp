// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <lib/ktrace.h>
#include <kernel/thread.h>
#include <platform.h>
#include <trace.h>

#include <inttypes.h>
#include <stdint.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

int sys_invalid_syscall(void) {
    LTRACEF("invalid syscall\n");
    return ERR_BAD_SYSCALL;
}

inline uint64_t invoke_syscall(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                               uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t arg7, uint64_t arg8) {
    uint64_t ret;

    switch (syscall_num) {
#include <magenta/syscall-invocation-cases.inc>
        default:
            ret = sys_invalid_syscall();
    }

    return ret;
}

#if ARCH_ARM64
#include <arch/arm64.h>

// N.B. Interrupts must be disabled on entry and they will be disabled on exit.
// The reason is the two calls two arch_curr_cpu_num in the ktrace calls: we
// don't want the cpu changing during the call.

extern "C" void arm64_syscall(struct arm64_iframe_long* frame, bool is_64bit, uint32_t syscall_imm, uint64_t pc) {
    uint64_t syscall_num = frame->r[16];

    ktrace_tiny(TAG_SYSCALL_ENTER, ((uint32_t)syscall_num << 8) | arch_curr_cpu_num());

    /* check for magic value to differentiate our syscalls */
    if (unlikely(syscall_imm != 0xf0f)) {
        LTRACEF("syscall does not have magenta magic, %#" PRIx64
                " @ PC %#" PRIx64 "\n", syscall_num, pc);
        frame->r[0] = ERR_BAD_SYSCALL;
        return;
    }

    THREAD_STATS_INC(syscalls);

    /* re-enable interrupts to maintain kernel preemptiveness
       This must be done after the above ktrace_tiny call, and after the
       above THREAD_STATS_INC call as it also calls arch_curr_cpu_num. */
    arch_enable_ints();

    LTRACEF_LEVEL(2, "num %" PRIu64 "\n", syscall_num);

    /* call the routine */
    uint64_t ret = invoke_syscall(syscall_num, frame->r[0], frame->r[1], frame->r[2], frame->r[3],
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

struct x86_64_syscall_result x86_64_syscall(
    uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4,
    uint64_t arg5, uint64_t arg6, uint64_t arg7, uint64_t arg8,
    uint64_t syscall_num, uint64_t ip) {

    thread_t* thread = get_current_thread();

    /* check for magic value to differentiate our syscalls */
    if (unlikely((syscall_num >> 32) != 0xff00ff)) {
        LTRACEF("syscall does not have magenta magic, %#" PRIx64
                " @ IP %#" PRIx64 "\n", syscall_num, ip);
        struct x86_64_syscall_result result;
        result.status = ERR_BAD_SYSCALL;
        result.is_signaled = thread_is_signaled(thread);
        return result;
    }
    syscall_num &= 0xffffffff;

    ktrace_tiny(TAG_SYSCALL_ENTER, (static_cast<uint32_t>(syscall_num) << 8) | arch_curr_cpu_num());

    THREAD_STATS_INC(syscalls);

    /* re-enable interrupts to maintain kernel preemptiveness
       This must be done after the above ktrace_tiny call, and after the
       above THREAD_STATS_INC call as it also calls arch_curr_cpu_num. */
    arch_enable_ints();

    LTRACEF_LEVEL(2, "t %p syscall num %" PRIu64 " ip %#" PRIx64 "\n",
                  thread, syscall_num, ip);

    uint64_t ret = invoke_syscall(syscall_num, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);

    LTRACEF_LEVEL(2, "t %p ret %#" PRIx64 "\n", thread, ret);

    /* re-disable interrupts on the way out
       This must be done before the below ktrace_tiny call. */
    arch_disable_ints();

    ktrace_tiny(TAG_SYSCALL_EXIT, (static_cast<uint32_t>(syscall_num) << 8) | arch_curr_cpu_num());

    struct x86_64_syscall_result result;
    result.status = ret;
    result.is_signaled = thread_is_signaled(thread);
    return result;
}

#endif
