// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_PLATFORM_H_
#define ZIRCON_KERNEL_INCLUDE_PLATFORM_H_

#include <sys/types.h>
#include <zircon/boot/crash-reason.h>
#include <zircon/boot/image.h>
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

/* current time in nanoseconds */
zx_time_t current_time(void);

/* high-precision timer ticks per second */
zx_ticks_t ticks_per_second(void);

/* Reads a platform-specific fixed-rate monotonic counter */
zx_ticks_t platform_current_ticks(void);

/* high-precision timer current_ticks */
static inline zx_ticks_t current_ticks(void) { return platform_current_ticks(); }

/* a bool indicating whether or not user mode has direct access to the registers
 * which allow directly observing the tick counter or not. */
bool platform_usermode_can_access_tick_registers(void);

/* super early platform initialization, before almost everything */
void platform_early_init(void);

/* Perform any set up required before virtual memory is enabled, or the heap is set up. */
void platform_prevm_init(void);

/* later init, after the kernel has come up */
void platform_init(void);

/* platform_panic_start informs the system that a panic message is about
 * to be printed and that platform_halt will be called shortly.  The
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
void platform_halt(platform_halt_action suggested_action, zircon_crash_reason_t reason) __NO_RETURN;

/* The platform specific actions to be taken in a halt situation.  This is a
 * weak symbol meant to be overloaded by platform specific implementations and
 * called from the common |platform_halt| implementation.  Do not call this
 * function directly, call |platform_halt| instead.
 *
 * There is no returning from this function.
 */
void platform_specific_halt(platform_halt_action suggested_action, zircon_crash_reason_t reason,
                            bool halt_on_panic) __NO_RETURN;

/* optionally stop the current cpu in a way the platform finds appropriate */
void platform_halt_cpu(void);

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

// Accessors for the HW reboot reason which may or may not have been delivered
// by the bootloader.
void platform_set_hw_reboot_reason(zbi_hw_reboot_reason_t reason);
zbi_hw_reboot_reason_t platform_hw_reboot_reason(void);

__END_CDECLS

#ifdef __cplusplus

#include <lib/arch/ticks.h>

#include <ktl/string_view.h>

// TODO(53594): Eventually `gCmdline` will be entirely replaced by
// `gBootOptions` and physboot will hand off the latter;
void ParseBootOptions(ktl::string_view cmdline);
void FinishBootOptions();

namespace affine {
class Ratio;  // Fwd decl.
}  // namespace affine

// Setter/getter pair for the ratio which defines the relationship between the
// system's tick counter, and the current_time/clock_monotonic clock.  This gets
// set once by architecture specific plaform code, after an appropriate ticks
// source has been selected and characterized.
void platform_set_ticks_to_time_ratio(const affine::Ratio& ticks_to_time);
const affine::Ratio& platform_get_ticks_to_time_ratio(void);

// Convert a sample taken early on to a proper zx_ticks_t, if possible.
// This returns 0 if early samples are not convertible.
zx_ticks_t platform_convert_early_ticks(arch::EarlyTicks sample);

#endif  // __cplusplus

#endif  // ZIRCON_KERNEL_INCLUDE_PLATFORM_H_
