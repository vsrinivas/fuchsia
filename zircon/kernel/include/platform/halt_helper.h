// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_PLATFORM_HALT_HELPER_H_
#define ZIRCON_KERNEL_INCLUDE_PLATFORM_HALT_HELPER_H_

#include <platform.h>
#include <zircon/boot/crash-reason.h>

// This function is used to coordinate concurrent halt/reboot operations.
//
// The idea is there's a single resource, the "halt token" and only the holder of the token may
// initiate a halt/reboot (except for panics).  This function attempts to acquire the token and
// signals an irrevocable intention to halt (or reboot) the system.
//
// If this function returns true, the caller has acquired the token and is now responsible for
// halting/reboot.
//
// If this function returns false, the caller failed to acquire the token (because some other caller
// got it).  In this case the caller must take no action and allow the holder to halt/reboot.
[[nodiscard]] bool TakeHaltToken();

// Gracefully halt and perform |action|.
//
// This function attempts to acquire the halt token.  If successful, it will perform |action| or
// panic if the system cannot be successfully halted before |panic_deadline| is reached.
//
// If the halt token cannot be acquired, this function will block forever.
void platform_graceful_halt_helper(platform_halt_action action, zircon_crash_reason_t,
                                   zx_time_t panic_deadline);

// Gracefully halt secondary (non-boot) CPUs.
//
// While the mechanism used is platform dependent, this function attempts to shut them down
// gracefully so that secondary CPUs aren't holding any kernel locks.
//
// Returns an error if all secondary CPU could not be not successfully shutdown before |deadline| is
// reached.
//
// This function must be called from the primary (boot) CPU.
zx_status_t platform_halt_secondary_cpus(zx_time_t deadline);

#endif  // ZIRCON_KERNEL_INCLUDE_PLATFORM_HALT_HELPER_H_
