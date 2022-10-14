// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_PDEV_HW_WATCHDOG_INCLUDE_PDEV_WATCHDOG_H_
#define ZIRCON_KERNEL_DEV_PDEV_HW_WATCHDOG_INCLUDE_PDEV_WATCHDOG_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// HW watchdog interface
typedef struct pdev_watchdog_ops {
  void (*pet)(void);
  zx_status_t (*set_enabled)(bool);
  bool (*is_enabled)(void);
  zx_duration_t (*get_timeout_nsec)(void);
  zx_time_t (*get_last_pet_time)(void);
  void (*suppress_petting)(bool);
  bool (*is_petting_suppressed)(void);
} pdev_watchdog_ops_t;

void pdev_register_watchdog(const pdev_watchdog_ops_t* ops);

__END_CDECLS

#endif  // ZIRCON_KERNEL_DEV_PDEV_HW_WATCHDOG_INCLUDE_PDEV_WATCHDOG_H_
