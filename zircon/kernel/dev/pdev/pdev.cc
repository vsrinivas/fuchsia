// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <lib/zbitl/view.h>
#include <stdio.h>

#include <lk/init.h>
#include <pdev/driver.h>
#include <pdev/pdev.h>
#include <phys/handoff.h>

extern const struct lk_pdev_init_struct __start_lk_pdev_init[];
extern const struct lk_pdev_init_struct __stop_lk_pdev_init[];

static void pdev_init_driver(uint32_t type, const void* driver_data, uint32_t length, uint level) {
  const struct lk_pdev_init_struct* ptr;
  for (ptr = __start_lk_pdev_init; ptr != __stop_lk_pdev_init; ptr++) {
    if (ptr->type == type && ptr->level == level) {
      ptr->hook(driver_data, length);
      return;
    }
  }
}

static void pdev_run_hooks(uint level) {
  zbitl::View zbi(ZbiInPhysmap());
  for (auto [header, payload] : zbi) {
    if (header->type == ZBI_TYPE_KERNEL_DRIVER) {
      // kernel driver type is in boot item extra
      pdev_init_driver(header->extra, payload.data(), header->length, level);
    }
  }
  ZX_ASSERT(zbi.take_error().is_ok());
}

void pdev_init() { pdev_run_hooks(LK_INIT_LEVEL_PLATFORM_EARLY); }

static void platform_dev_init(uint level) { pdev_run_hooks(level); }

LK_INIT_HOOK(platform_dev_init, platform_dev_init, LK_INIT_LEVEL_PLATFORM)
