// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013 Google Inc.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_WATCHDOG_INCLUDE_LIB_WATCHDOG_H_
#define ZIRCON_KERNEL_LIB_WATCHDOG_INCLUDE_LIB_WATCHDOG_H_

#include <stdbool.h>
#include <stdint.h>
#include <zircon/types.h>

#include <kernel/thread.h>
#include <kernel/timer.h>

#define WATCHDOG_MAGIC 'wdog'

typedef struct watchdog {
  uint32_t magic;
  const char *name;
  bool enabled;
  zx_duration_t timeout;
  timer_t expire_timer;
} watchdog_t;

/* HW watchdog support.  This is nothing but a simple helper used to
 * automatically dismiss a platform's HW watchdog using LK timers.  Platforms
 * must supply
 *
 * platform_watchdog_init
 * platform_watchdog_set_enabled
 * platform_watchdog_pet
 *
 * in order to use the HW watchdog helper functions.  After initialized, users
 * may enable and disable the HW watchdog whenever appropriate.  The helper will
 * maintain a timer which dismisses the watchdog at the pet interval recommended
 * by the platform.  Any programming error which prevents the scheduler timer
 * mechanism from running properly will eventually result in the watchdog firing
 * and the system rebooting.  Whenever possible, when using SW based watchdogs,
 * it is recommended that systems provide platform support for a HW watchdog and
 * enable the HW watchdog.  SW watchdogs are based on LK timers, and should be
 * reliable as long as the scheduler and timer mechanism is running properly;
 * the HW watchdog functionality provided here should protect the system in case
 * something managed to break timers on LK.
 */

extern zx_status_t platform_watchdog_init(zx_duration_t target_timeout,
                                          zx_duration_t *recommended_pet_period);
extern void platform_watchdog_set_enabled(bool enabled);
extern void platform_watchdog_pet(void);

zx_status_t watchdog_hw_init(zx_duration_t timeout);
void watchdog_hw_set_enabled(bool enabled);

#endif  // ZIRCON_KERNEL_LIB_WATCHDOG_INCLUDE_LIB_WATCHDOG_H_
