#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <lk/init.h>
#include <kernel/cmdline.h>
#include <kernel/timer.h>
#include <platform.h>
#include <platform/keyboard.h>
#include "platform_p.h"

static volatile lk_time_t watchdog_last_time = 0;

#define WATCHDOG_CHECKIN_PERIOD_NS LK_MSEC(50)

// If the LK timer hasn't checked in in this long, reset the machine.
#define ASSUMED_DEAD_PERIOD_NS LK_MSEC(150)

void platform_handle_watchdog(void);
void platform_handle_watchdog(void)
{
    lk_time_t last_time = watchdog_last_time;

    lk_time_t now = current_time();
    lk_time_t deadline = last_time + ASSUMED_DEAD_PERIOD_NS;
    if (now < last_time - LK_MSEC(2) || now > deadline) {
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
    timer_set_oneshot(t, watchdog_last_time + WATCHDOG_CHECKIN_PERIOD_NS, checkin_callback, nullptr);
    watchdog_last_time = now;
    return INT_NO_RESCHEDULE;
}

static void install_watchdog_timer(uint level)
{
    static timer_t watchdog_timer = TIMER_INITIAL_VALUE(watchdog_timer);

    if (cmdline_get_bool("kernel.watchdog", false)) {
        watchdog_last_time = current_time();
        timer_set_oneshot(&watchdog_timer, watchdog_last_time + WATCHDOG_CHECKIN_PERIOD_NS, checkin_callback, nullptr);

        platform_configure_watchdog(20); // 50ms granularity
    }
}
LK_INIT_HOOK(watchdog, &install_watchdog_timer, LK_INIT_LEVEL_KERNEL);
