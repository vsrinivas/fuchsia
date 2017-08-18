// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
//

#include <stdio.h>
#include <string.h>
#include <arch/mp.h>
#include <arch/x86.h>
#include <arch/x86/mp.h>

#include <platform.h>
#include <platform/keyboard.h>

#include <lib/console.h>
#include <lib/version.h>
#include <arch/x86/apic.h>

#if WITH_LIB_DEBUGLOG
#include <lib/debuglog.h>
#endif

static void reboot(void) {
    // Try legacy reboot path first
    pc_keyboard_reboot();

    // Try 100-Series Chipset Reset Control Register: CPU + SYS Reset
    outp(0xCF9, 0x06);
}

static volatile int panic_started;

static void halt_other_cpus(void) {
    static volatile int halted = 0;

    if (atomic_swap(&halted, 1) == 0) {
        // stop the other cpus
        printf("stopping other cpus\n");
        arch_mp_send_ipi(MP_IPI_TARGET_ALL_BUT_LOCAL, 0, MP_IPI_HALT);

        // spin for a while
        // TODO: find a better way to spin at this low level
        for (volatile int i = 0; i < 100000000; i++) {
            __asm volatile ("nop");
        }
    }
}

void platform_halt_cpu(void)
{
    apic_send_self_ipi(0x00, DELIVERY_MODE_INIT);
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

bool halt_on_panic = false;

void platform_halt(
        platform_halt_action suggested_action,
        platform_halt_reason reason)
{
    printf("platform_halt suggested_action %d reason %d\n", suggested_action, reason);

    arch_disable_ints();

    switch (suggested_action) {
        case HALT_ACTION_SHUTDOWN:
            printf("Power off failed, halting\n");
            break;
        case HALT_ACTION_REBOOT:
            printf("Rebooting...\n");
            reboot();
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
#endif
    dlog_bluescreen_halt();
#endif

    if (!halt_on_panic) {
        printf("Rebooting...\n");
        reboot();
    }

    printf("Halted\n");

#if ENABLE_PANIC_SHELL
    panic_shell_start();
#endif

    for (;;) {
        x86_hlt();
    }
}
