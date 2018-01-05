// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012 Google, Inc.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <err.h>
#include <kernel/thread.h>
#include <lib/console.h>
#include <platform.h>
#include <platform/debug.h>
#include <stdio.h>
#include <zircon/compiler.h>

/*
 * default implementations of these routines, if the platform code
 * chooses not to implement.
 */
__WEAK void platform_halt(platform_halt_action suggested_action,
                          platform_halt_reason reason) {

    thread_print_current_backtrace();

#if ENABLE_PANIC_SHELL
    if (reason == HALT_REASON_SW_PANIC) {
        dprintf(ALWAYS, "CRASH: starting debug shell... (reason = %d)\n", reason);
        arch_disable_ints();
        panic_shell_start();
    }
#endif // ENABLE_PANIC_SHELL

    dprintf(ALWAYS, "HALT: spinning forever... (reason = %d)\n", reason);
    arch_disable_ints();
    for (;;) {
    }
}

__WEAK void platform_halt_cpu() {
}

__WEAK void platform_halt_secondary_cpus() {
    PANIC_UNIMPLEMENTED;
}

#if WITH_LIB_CONSOLE

#include <lib/console.h>

static int cmd_reboot(int argc, const cmd_args* argv, uint32_t flags) {
    platform_halt(HALT_ACTION_REBOOT, HALT_REASON_SW_RESET);
    return 0;
}

static int cmd_reboot_bootloader(int argc, const cmd_args* argv, uint32_t flags) {
    platform_halt(HALT_ACTION_REBOOT_BOOTLOADER, HALT_REASON_SW_RESET);
    return 0;
}

static int cmd_poweroff(int argc, const cmd_args* argv, uint32_t flags) {
    platform_halt(HALT_ACTION_SHUTDOWN, HALT_REASON_SW_RESET);
    return 0;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 1
STATIC_COMMAND_MASKED("reboot", "soft reset", &cmd_reboot, CMD_AVAIL_ALWAYS)
STATIC_COMMAND_MASKED("reboot-bootloader", "reboot into bootloader", &cmd_reboot_bootloader, CMD_AVAIL_ALWAYS)
STATIC_COMMAND_MASKED("poweroff", "powerdown", &cmd_poweroff, CMD_AVAIL_ALWAYS)
#endif
STATIC_COMMAND_END(platform_power);

#endif
