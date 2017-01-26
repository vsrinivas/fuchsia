// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>

#include <lib/ktrace.h>
#include <lib/user_copy.h>

#include <magenta/magenta.h>
#include <magenta/state_tracker.h>
#include <magenta/user_copy.h>
#include <magenta/user_thread.h>

#include <inttypes.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

int sys_invalid_syscall(void) {
    LTRACEF("invalid syscall\n");
    return ERR_BAD_SYSCALL;
}

#define MAGENTA_VDSOCALL_DEF(ret, name, args...) // Nothing to do here.

#if ARCH_ARM64
#include <arch/arm64.h>

using syscall_func = int64_t (*)(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,
                                 uint64_t f, uint64_t g, uint64_t h);

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

    /* re-enable interrupts to maintain kernel preemptiveness */
    arch_enable_ints();

    LTRACEF_LEVEL(2, "num %" PRIu64 "\n", syscall_num);

    /* build a function pointer to call the routine.
     * the args are jammed into the function independent of if the function
     * uses them or not, which is safe for simple arg passing.
     */
    syscall_func sfunc;

    switch (syscall_num) {
#define MAGENTA_SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...)                               \
    case n:                                                                                        \
        sfunc = reinterpret_cast<syscall_func>(sys_##name);                                        \
        break;
#include <magenta/gen-switch.inc>
        default:
            sfunc = reinterpret_cast<syscall_func>(sys_invalid_syscall);
    }

    /* call the routine */
    uint64_t ret = sfunc(frame->r[0], frame->r[1], frame->r[2], frame->r[3], frame->r[4],
                         frame->r[5], frame->r[6], frame->r[7]);

    LTRACEF_LEVEL(2, "ret %#" PRIx64 "\n", ret);

    /* put the return code back */
    frame->r[0] = ret;

    /* check to see if there are any pending signals */
    thread_process_pending_signals();

    /* re-disable interrupts on the way out */
    arch_disable_ints();

    ktrace_tiny(TAG_SYSCALL_EXIT, ((uint32_t)syscall_num << 8) | arch_curr_cpu_num());
}

#endif

#if ARCH_X86_64
#include <arch/x86.h>

using syscall_func = int64_t (*)(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,
                                 uint64_t f, uint64_t g, uint64_t h);

uint64_t x86_64_syscall(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4,
                        uint64_t arg5, uint64_t arg6, uint64_t arg7, uint64_t arg8,
                        uint64_t syscall_num, uint64_t ip) {

    /* check for magic value to differentiate our syscalls */
    if (unlikely((syscall_num >> 32) != 0xff00ff)) {
        LTRACEF("syscall does not have magenta magic, %#" PRIx64
                " @ IP %#" PRIx64 "\n", syscall_num, ip);
        return ERR_BAD_SYSCALL;
    }
    syscall_num &= 0xffffffff;

    ktrace_tiny(TAG_SYSCALL_ENTER, (static_cast<uint32_t>(syscall_num) << 8) | arch_curr_cpu_num());

    /* re-enable interrupts to maintain kernel preemptiveness */
    arch_enable_ints();

    LTRACEF_LEVEL(2, "t %p syscall num %" PRIu64 " ip %#" PRIx64 "\n",
                  get_current_thread(), syscall_num, ip);

    /* build a function pointer to call the routine.
     * the args are jammed into the function independent of if the function
     * uses them or not, which is safe for simple arg passing.
     */
    syscall_func sfunc;

    switch (syscall_num) {
#define MAGENTA_SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...)                               \
    case n:                                                                                        \
        sfunc = reinterpret_cast<syscall_func>(sys_##name);                                        \
        break;
#include <magenta/gen-switch.inc>
        default:
            sfunc = reinterpret_cast<syscall_func>(sys_invalid_syscall);
    }

    /* call the routine */
    uint64_t ret = sfunc(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);

    /* check to see if there are any pending signals */
    thread_process_pending_signals();

    LTRACEF_LEVEL(2, "t %p ret %#" PRIx64 "\n", get_current_thread(), ret);

    arch_disable_ints();
    ktrace_tiny(TAG_SYSCALL_EXIT, (static_cast<uint32_t>(syscall_num) << 8) | arch_curr_cpu_num());

    return ret;
}

#endif
