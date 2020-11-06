// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013 Google Inc.
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <inttypes.h>
#include <lib/watchdog.h>
#include <platform.h>
#include <zircon/boot/crash-reason.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <kernel/auto_lock.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <kernel/timer.h>

static SpinLock lock;

static timer_t hw_watchdog_timer;
static bool hw_watchdog_enabled;
static zx_duration_t hw_watchdog_pet_timeout;

static void hw_watchdog_timer_callback(timer_t* timer, zx_time_t now, void* arg) {
  timer_set_oneshot(timer, zx_time_add_duration(now, hw_watchdog_pet_timeout),
                    hw_watchdog_timer_callback, NULL);
  platform_watchdog_pet();
}

zx_status_t watchdog_hw_init(zx_time_t timeout) {
  DEBUG_ASSERT(ZX_TIME_INFINITE != timeout);
  timer_init(&hw_watchdog_timer);
  return platform_watchdog_init(timeout, &hw_watchdog_pet_timeout);
}

void watchdog_hw_set_enabled(bool enabled) {
  AutoSpinLock guard(&lock);

  if (hw_watchdog_enabled == enabled) {
    return;
  }

  hw_watchdog_enabled = enabled;
  platform_watchdog_set_enabled(enabled);
  if (enabled) {
    timer_set_oneshot(&hw_watchdog_timer,
                      zx_time_add_duration(current_time(), hw_watchdog_pet_timeout),
                      hw_watchdog_timer_callback, NULL);
  } else {
    timer_cancel(&hw_watchdog_timer);
  }
}
