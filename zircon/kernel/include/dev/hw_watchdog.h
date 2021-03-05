// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_DEV_HW_WATCHDOG_H_
#define ZIRCON_KERNEL_INCLUDE_DEV_HW_WATCHDOG_H_

#include <stdbool.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Returns true if this platform has a hardware watchdog, false otherwise.
bool hw_watchdog_present(void);

// Pet the hardware watchdog if present and petting is not suppressed.
void hw_watchdog_pet(void);

// Attempt to enable or disable the hardware watchdog.  Note that depending on
// hardware details, it may not be possible to change its enabled/disabled
// state.
zx_status_t hw_watchdog_set_enabled(bool enabled);

// Returns true if this platform has a hardware watchdog, and that watchdog is
// currently enabled.
bool hw_watchdog_is_enabled(void);

// Returns the nominal timeout period of the hardware watchdog.
zx_duration_t hw_watchdog_get_timeout_nsec(void);

// Returns the last time at which the hardware watchdog was successfully pet.
zx_time_t hw_watchdog_get_last_pet_time(void);

// When |suppress| is true, prevent any thread from actually petting the
// watchdog.  Otherwise, permit threads to pet the watchdog.  This feature is
// used when the system is attempting to create a crashlog and reboot during a
// software watchdog panic.  At the start of this process, HW watchdog petting
// is suppressed to make sure that even if one or more cores is functioning,
// that they cannot pet the watchdog while the core attempting to reboot is
// building the crasholg.  This way, if the core attempting to reboot somehow
// locks up, the HW watchdog will fire as a last resort.
void hw_watchdog_suppress_petting(bool suppress);

// Returns true of watchdog petting suppression is enabled, false otherwise.
bool hw_watchdog_is_petting_suppressed(void);

__END_CDECLS

#endif  // ZIRCON_KERNEL_INCLUDE_DEV_HW_WATCHDOG_H_
