// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <zircon/types.h>

#include <arch/arch_ops.h>
#include <dev/watchdog.h>
#include <pdev/watchdog.h>

static const struct pdev_watchdog_ops default_ops = {
    .pet = []() {},
    .set_enabled = [](bool) { return ZX_ERR_NOT_SUPPORTED; },
    .is_enabled = []() { return false; },
    .get_timeout_nsec = []() -> zx_duration_t { return ZX_TIME_INFINITE; },
    .get_last_pet_time = []() -> zx_time_t { return 0; },
};

static const struct pdev_watchdog_ops* watchdog_ops = &default_ops;

bool watchdog_present() { return watchdog_ops != &default_ops; }
void watchdog_pet() { watchdog_ops->pet(); }
zx_status_t watchdog_set_enabled(bool enabled) { return watchdog_ops->set_enabled(enabled); }
bool watchdog_is_enabled() { return watchdog_ops->is_enabled(); }
zx_duration_t watchdog_get_timeout_nsec() { return watchdog_ops->get_timeout_nsec(); }
zx_time_t watchdog_get_last_pet_time() { return watchdog_ops->get_last_pet_time(); }

void pdev_register_watchdog(const pdev_watchdog_ops_t* ops) {
  watchdog_ops = ops;
  smp_mb();
}
