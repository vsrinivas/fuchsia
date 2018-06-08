// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define BOOT_CPU_ID 0

typedef enum {
    HALT_ACTION_HALT = 0,           // Spin forever.
    HALT_ACTION_REBOOT,             // Reset the CPU.
    HALT_ACTION_REBOOT_BOOTLOADER,  // Reboot into the bootloader.
    HALT_ACTION_REBOOT_RECOVERY,    // Reboot into the recovery partition.
    HALT_ACTION_SHUTDOWN,           // Shutdown and power off.
} platform_halt_action;

typedef enum {
    HALT_REASON_UNKNOWN = 0,
    HALT_REASON_SW_RESET,       // Generic Software Initiated Reboot
    HALT_REASON_SW_PANIC,       // Reboot triggered by a SW panic or ASSERT
} platform_halt_reason;

/* current time in nanoseconds */
zx_time_t current_time(void);

/* high-precision timer ticks per second */
zx_ticks_t ticks_per_second(void);

/* high-precision timer current_ticks */
zx_ticks_t current_ticks(void);

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

/* platform_halt halts the system and performs the |suggested_action|.
 *
 * This function is used in both the graceful shutdown and panic paths so it
 * does not perform more complex actions like switching to the primary CPU,
 * unloading the run queue of secondary CPUs, stopping secondary CPUs, etc.
 *
 * There is no returning from this function.
 */
void platform_halt(platform_halt_action suggested_action,
                   platform_halt_reason reason) __NO_RETURN;

/* optionally stop the current cpu in a way the platform finds appropriate */
void platform_halt_cpu(void);

/* platform_halt_secondary_cpus halts secondary (non-boot) CPUs.
 *
 * While the mechanism used is platform dependent, this function attempts to shut them down
 * gracefully so that secondary CPUs aren't holding any spinlocks.
 *
 * This function must be called from the primary (boot) CPU.
 */
void platform_halt_secondary_cpus(void);

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

/* If len == 0, return the length of the last crashlog (or 0 if none).
 * Otherwise call func() to return the last crashlog to the caller,
 * returning the length the last crashlog.
 *
 * func() may be called as many times as necessary (adjusting off)
 * to return the crashlog in segments.  There will not be gaps,
 * but the individual segments may range from 1 byte to the full
 * length requested, depending on the limitations of the underlying
 * storage model.
 */
size_t platform_recover_crashlog(size_t len, void* cookie,
                                 void (*func)(const void* data, size_t off, size_t len, void* cookie));

// Called just before initiating a system suspend to give the platform layer a
// chance to save state.  Must be called with interrupts disabled.
void platform_suspend(void);

// Called immediately after resuming from a system suspend to let the platform layer
// reinitialize arch components.  Must be called with interrupts disabled.
void platform_resume(void);

// Returns true if this system has a debug serial port that is enabled
bool platform_serial_enabled(void);

// Returns true if the early graphics console is enabled
bool platform_early_console_enabled(void);

__END_CDECLS
