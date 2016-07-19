// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
//

#include <arch/x86.h>
#include <platform.h>
#include <platform/pc/acpi.h>
#include <platform/keyboard.h>

void platform_halt(
        platform_halt_action suggested_action,
        platform_halt_reason reason)
{
    switch (suggested_action) {
        case HALT_ACTION_SHUTDOWN:
            printf("Powering off...\n");
            acpi_poweroff();
            printf("Power off failed, halting\n");
            break;
        case HALT_ACTION_REBOOT:
            printf("Rebooting...\n");
            acpi_reboot();
            pc_keyboard_reboot();
            printf("Reboot failed, halting\n");
            break;
        case HALT_ACTION_HALT:
            printf("Halting...\n");
            break;
    }

    for (;;) {
        x86_cli();
        x86_hlt();
    }
}
