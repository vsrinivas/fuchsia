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

__BEGIN_CDECLS

bool hw_watchdog_present(void);
void hw_watchdog_pet(void);
zx_status_t hw_watchdog_set_enabled(bool enabled);
bool hw_watchdog_is_enabled(void);
zx_duration_t hw_watchdog_get_timeout_nsec(void);
zx_time_t hw_watchdog_get_last_pet_time(void);

__END_CDECLS

#endif  // ZIRCON_KERNEL_INCLUDE_DEV_HW_WATCHDOG_H_
