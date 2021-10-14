// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/irq.h"

#include <lib/async/dispatcher.h>

#include <memory>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/task-internal.h"

// Internally, the IRQ timers are just tasks, dispatched on the IRQ dispatcher.
struct iwl_irq_timer : public wlan::iwlwifi::TaskInternal {
 public:
  explicit iwl_irq_timer(async_dispatcher_t* dispatcher, iwl_irq_timer_func func, void* data)
      : TaskInternal(dispatcher, func, data) {}
};

zx_status_t iwl_irq_timer_create(struct device* dev, iwl_irq_timer_func func, void* data,
                                 struct iwl_irq_timer** out_timer) {
  *out_timer = new iwl_irq_timer(dev->irq_dispatcher, func, data);
  return ZX_OK;
}

zx_status_t iwl_irq_timer_start(struct iwl_irq_timer* timer, zx_duration_t delay) {
  return timer->Post(delay);
}

zx_status_t iwl_irq_timer_stop(struct iwl_irq_timer* timer) { return timer->Cancel(); }

void iwl_irq_timer_release_sync(struct iwl_irq_timer* timer) { delete timer; }
