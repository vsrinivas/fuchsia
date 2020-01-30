// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_DEV_WATCHDOG_H_
#define ZIRCON_KERNEL_INCLUDE_DEV_WATCHDOG_H_

#include <stdbool.h>
#include <sys/types.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

bool watchdog_present(void);
void watchdog_pet(void);
zx_status_t watchdog_set_enabled(bool enabled);
bool watchdog_is_enabled(void);
zx_duration_t watchdog_get_timeout_nsec(void);
zx_time_t watchdog_get_last_pet_time(void);

__END_CDECLS

#endif  // ZIRCON_KERNEL_INCLUDE_DEV_WATCHDOG_H_
