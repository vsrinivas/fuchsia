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
#include <lib/console.h>

#if WITH_LIB_DEBUGLOG
#include <lib/debuglog.h>
#endif

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

void platform_panic_start(void) {
    arch_disable_ints();

    halt_other_cpus();

    if (atomic_swap(&panic_started, 1) == 0) {
#if WITH_LIB_DEBUGLOG
        dlog_bluescreen_init();
#endif
    }
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
