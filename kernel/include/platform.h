// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef __PLATFORM_H
#define __PLATFORM_H

#include <sys/types.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS;

typedef enum {
    HALT_ACTION_HALT = 0,       // Spin forever.
    HALT_ACTION_REBOOT,         // Reset the CPU.
    HALT_ACTION_SHUTDOWN,       // Shutdown and power off.
} platform_halt_action;

typedef enum {
    HALT_REASON_UNKNOWN = 0,
    HALT_REASON_POR,            // Cold-boot
    HALT_REASON_HW_WATCHDOG,    // HW watchdog timer
    HALT_REASON_LOWVOLTAGE,     // LV/Brownout condition
    HALT_REASON_HIGHVOLTAGE,    // High voltage condition.
    HALT_REASON_THERMAL,        // Thermal reason (probably overtemp)
    HALT_REASON_OTHER_HW,       // Other hardware (platform) specific reason
    HALT_REASON_SW_RESET,       // Generic Software Initiated Reboot
    HALT_REASON_SW_WATCHDOG,    // Reboot triggered by a SW watchdog timer
    HALT_REASON_SW_PANIC,       // Reboot triggered by a SW panic or ASSERT
    HALT_REASON_SW_UPDATE,      // SW triggered reboot in order to begin firmware update
} platform_halt_reason;

/* current time in nanoseconds */
lk_time_t current_time(void);

/* high-precision timer ticks per second */
uint64_t ticks_per_second(void);

/* super early platform initialization, before almost everything */
void platform_early_init(void);

/* later init, after the kernel has come up */
void platform_init(void);

/* called by the arch init code to get the platform to set up any mmu mappings it may need */
void platform_init_mmu_mappings(void);

/* if the platform has knowledge of what caused the latest reboot, it can report
 * it to applications with this function.  */
platform_halt_reason platform_get_reboot_reason(void);


/* platform_panic_start informs the system that a panic message is about
 * to be printed and that platformn_halt will be called shortly.  The
 * platform should stop other CPUs if possible and do whatever is necessary
 * to safely ensure that the panic message will be visible to the user.
 */
void platform_panic_start(void);

/* platform_halt is a method which is called from various places in the LK
 * system, and may be implemented by platforms and called by applications.  This
 * call represents the end of the life of SW for a device; there is no returning
 * from this function.  Callers will provide a reason for the halt, and a
 * suggested action for the platform to take, but it is the platform's
 * responsibility to determine the final action taken.  For example, in the case
 * of a failed ASSERT or a panic, LK will call platform halt and suggest a Halt
 * action, but a release build on a platform with no debug channel may choose to
 * reboot instead as there is no one to tell about the ASSERT, and no one
 * waiting to debug the device in its halted state.  If not overloaded by the
 * platform, the default behavior of platform halt will be to dprintf the
 * reason, and then halt execution by turning off interrupts and spinning
 * forever.
 */
void platform_halt(platform_halt_action suggested_action,
                   platform_halt_reason reason) __NO_RETURN;

/* optionally stop the current cpu in a way the platform finds appropriate */
void platform_halt_cpu(void);

/* called during chain loading to make sure drivers and platform is put into a stopped state */
void platform_quiesce(void);

/* returns pointer to ramdisk image, or NULL if none.
 * Sets size to ramdisk size or zero if none.
 */
void *platform_get_ramdisk(size_t *size);

/* Stash the crashlog somewhere platform-specific that allows
 * for recovery after reboot.  This will only be called out
 * of the panic() handling path on the way to reboot, and is
 * not necessarily safe to be called from any other state.
 *
 * Calling with a NULL log returns the maximum supported size.
 * It is safe to query the size at any time after boot.  If the
 * return is 0, no crashlog recovery is supported.
 */
size_t platform_stow_crashlog(void* log, size_t len);

__END_CDECLS;

#endif
