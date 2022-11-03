// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_IRQ_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_IRQ_H_

// This file defines the IRQ timer interface, equivalent to timer_setup() and friends in Linux.
// These are tasks that are run in Linux in interrupt context; as such these tasks are not allowed
// to sleep, block, etc.

#include <zircon/types.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/kernel.h"

#if defined(__cplusplus)
extern "C" {
#endif  // defined(__cplusplus)

struct iwl_irq_timer;
typedef void (*iwl_irq_timer_func)(void* data);

// Create a timer.
zx_status_t iwl_irq_timer_create(struct device* dev, iwl_irq_timer_func func, void* data,
                                 struct iwl_irq_timer** out_timer);

// Start a timer, to be run after `delay`.  If the timer is already started, it will be stopped
// first.
zx_status_t iwl_irq_timer_start(struct iwl_irq_timer* timer, zx_duration_t delay);

// Start a timer to be run at `time`. If the timer is already started, it will be stopped first.
// If `time` is in the past, ZX_ERR_INVALID_ARGS will be returned.
zx_status_t iwl_irq_timer_start_at_time(struct iwl_irq_timer* timer, zx_time_t time);

// Cancel the timer.  This call does not block: once this call returns, the timer is no longer
// queued for execution; it has either been dequeued and dispatched (but may be currently
// executing), or cancelled.
// * ZX_OK if the task was cancelled.
// * ZX_ERR_NOT_FOUND if the task was not queued and thus not cancelled.
// * Other errors in other error cases.
zx_status_t iwl_irq_timer_stop(struct iwl_irq_timer* timer);

// Blocks until the timer task completes.
zx_status_t iwl_irq_timer_wait(struct iwl_irq_timer* timer);

// Release (and deallocate) the timer, synchronously.  If the timer is running, it will be
// cancelled.
void iwl_irq_timer_release_sync(struct iwl_irq_timer* timer);

#if defined(__cplusplus)
}  // extern "C"
#endif  // defined(__cplusplus)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_IRQ_H_
