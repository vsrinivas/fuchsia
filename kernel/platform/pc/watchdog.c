#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <lk/init.h>
#include <kernel/cmdline.h>
#include <kernel/timer.h>
#include <platform.h>
#include <platform/keyboard.h>
#include "platform_p.h"

static volatile uint64_t watchdog_last_time = 0;

#define WATCHDOG_CHECKIN_PERIOD_MS 50

// If the LK timer hasn't checked in in this long, reset the machine.
#define ASSUMED_DEAD_PERIOD_MS 150

void platform_handle_watchdog(void);
void platform_handle_watchdog(void)
{
    uint64_t last_time = watchdog_last_time;

    uint64_t now = current_time();
    uint64_t deadline = last_time + ASSUMED_DEAD_PERIOD_MS;
    if (now < last_time - 2 || now > deadline) {
        // Shoot all other cores
        apic_send_broadcast_ipi(0, DELIVERY_MODE_INIT);

        // Try a legacy reset
        pc_keyboard_reboot();

        // Trigger a full reset of the system via the Reset Control Register
        outp(0xCF9, 0x07);
        // Wait a second for the command to process before declaring failure
        spin(1000000);

        // Triple fault
        uint8_t garbage_idtr[10] = { 0 };
        __asm volatile(
                "lidt %0\r\n"
                "int $2\r\n"
                :
                : "m"(garbage_idtr)
                : "memory"
                );
    }

    apic_issue_eoi();
}

static enum handler_return checkin_callback(struct timer *t, lk_time_t now, void *arg)
{
    watchdog_last_time = current_time();
    return INT_NO_RESCHEDULE;
}

static timer_t watchdog_timer = TIMER_INITIAL_VALUE(watchdog_timer);
static void install_watchdog_timer(uint level)
{
    if (cmdline_get_bool("kernel.watchdog", false)) {
        watchdog_last_time = current_time();
        timer_set_periodic(&watchdog_timer, WATCHDOG_CHECKIN_PERIOD_MS, checkin_callback, NULL);

        platform_configure_watchdog(20); // 50ms granularity
    }
}
LK_INIT_HOOK(watchdog, &install_watchdog_timer, LK_INIT_LEVEL_KERNEL);
