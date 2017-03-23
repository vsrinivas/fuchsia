// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
//

#include <stdio.h>
#include <arch/mp.h>
#include <arch/x86.h>
#include <arch/x86/mp.h>

#include <platform.h>
#include <platform/keyboard.h>

#include <lib/capsule.h>
#include <lib/console.h>

#if WITH_LIB_DEBUGLOG
#include <lib/debuglog.h>
#endif

#include "git-version.h"

static volatile int panic_started;

static void halt_other_cpus(void) {
#if WITH_SMP
    static volatile int halted = 0;

    if (atomic_swap(&halted, 1) == 0) {
        // stop the other cpus
        printf("stopping other cpus\n");
        arch_mp_send_ipi(MP_CPU_ALL_BUT_LOCAL, MP_IPI_HALT);

        // spin for a while
        // TODO: find a better way to spin at this low level
        for (volatile int i = 0; i < 100000000; i++) {
            __asm volatile ("nop");
        }
    }
#endif
}

#define CAPSULE_TAG_x86_64_CRASH 33
#define CAPSULE_BULID_DIGITS 8
#define CAPSULE_TAG_MAGIC_x86 0x3e343658

#pragma pack(push, 1)
typedef struct x86_panic_capsule {
    uint32_t magic;
    char build[CAPSULE_BULID_DIGITS];
    uint32_t count;
    uint32_t pc32[THREAD_BACKTRACE_DEPTH];
} x86_panic_capsule_t;
#pragma pack(pop)

static void store_panic_in_capsule(void) {
    thread_backtrace_t bt;
    int count = thread_get_backtrace(get_current_thread(), __GET_FRAME(0), &bt);
    if (count < 1) {
        printf("failed to get backtrace\n");
        return;
    }
    x86_panic_capsule_t capsule = {
        .magic = CAPSULE_TAG_MAGIC_x86,
        .count = (uint32_t)count,
    };

    for (int c = 0; c < CAPSULE_BULID_DIGITS; c++) {
        capsule.build[c] = MAGENTA_GIT_REV[c];
    }
    for (int c = 0; c < THREAD_BACKTRACE_DEPTH; c++) {
        capsule.pc32[c] = (c < count) ?
            (uint32_t)((uintptr_t)bt.pc[c] & 0x00000000ffffffff) : 0u;
    }
    int32_t rc = capsule_store(
        CAPSULE_TAG_x86_64_CRASH, &capsule, sizeof(x86_panic_capsule_t));
    if (rc < 0) {
        printf("store to capsule failed: %d\n", rc);
    } else {
        printf("stored %d frames in capsule\n", count);
    }
}

void platform_panic_start(void) {
    arch_disable_ints();

    if (atomic_swap(&panic_started, 1) == 0) {
#if WITH_LIB_DEBUGLOG
        dlog_bluescreen_init();
#endif
    }

    halt_other_cpus();
}

void platform_halt(
        platform_halt_action suggested_action,
        platform_halt_reason reason)
{
    printf("platform_halt suggested_action %u reason %u\n", suggested_action, reason);

    arch_disable_ints();

    switch (suggested_action) {
        case HALT_ACTION_SHUTDOWN:
            printf("Power off failed, halting\n");
            break;
        case HALT_ACTION_REBOOT:
            printf("Rebooting...\n");
            pc_keyboard_reboot();
            printf("Reboot failed, halting\n");
            break;
        case HALT_ACTION_HALT:
            printf("Halting...\n");

            halt_other_cpus();
            break;
    }

#if WITH_LIB_DEBUGLOG
#if WITH_PANIC_BACKTRACE
    thread_print_backtrace(get_current_thread(), __GET_FRAME(0));
    store_panic_in_capsule();
#endif
    dlog_bluescreen_halt();
#endif

    printf("Halted\n");

#if ENABLE_PANIC_SHELL
    panic_shell_start();
#endif

    for (;;) {
        x86_hlt();
    }
}

#if WITH_LIB_CONSOLE

static int cmd_prevcrash(int argc, const cmd_args *argv, uint32_t flags) {
    x86_panic_capsule_t capsule;
    int32_t rv = capsule_fetch(
        CAPSULE_TAG_x86_64_CRASH, &capsule, sizeof(x86_panic_capsule_t));
    if (capsule.magic != CAPSULE_TAG_MAGIC_x86) {
        printf("panic capsule is corrupt (%d) %x\n", rv, capsule.magic);
        return 0;
    }
    printf("panic capsule found\nbuild:");
    for (int c = 0; c < CAPSULE_BULID_DIGITS; c++) {
        printf("%c", capsule.build[c]);
    }
    printf("\nbacktrace:\n");
    for (int c = 0; c < THREAD_BACKTRACE_DEPTH; c++) {
        if (c < (int)capsule.count)
            printf("bt#%02d: %p\n", c, (void*)(capsule.pc32[c] | 0xffffffff00000000));
    }
    printf("\n");
    return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("prevpanic", "dump panic stored in capsule", &cmd_prevcrash)
STATIC_COMMAND_END(prevpanic);

#endif  // WITH_LIB_CONSOLE

