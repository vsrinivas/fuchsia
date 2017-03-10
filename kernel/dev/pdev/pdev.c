// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>
#include <pdev/driver.h>
#include <pdev/pdev.h>
#include <lk/init.h>

static mdi_node_ref_t driver_list = {0};

extern const struct lk_pdev_init_struct __start_lk_pdev_init[] __WEAK;
extern const struct lk_pdev_init_struct __stop_lk_pdev_init[] __WEAK;

static void pdev_init_driver(mdi_node_ref_t* node_ref, uint level) {
    mdi_id_t id = mdi_id(node_ref);

    for (const struct lk_pdev_init_struct *ptr = __start_lk_pdev_init; ptr != __stop_lk_pdev_init; ptr++) {
        if (ptr->id == id && ptr->level == level) {
            ptr->hook(node_ref, level);
            return;
        }
    }
}

static void pdev_run_hooks(uint level) {
    if (!mdi_valid(&driver_list)) return;

    mdi_node_ref_t driver;
    mdi_each_child(&driver_list, &driver) {
        pdev_init_driver(&driver, level);
    }
}

void pdev_init(const mdi_node_ref_t* drivers) {
    ASSERT(drivers);
    driver_list = *drivers;

    pdev_run_hooks(LK_INIT_LEVEL_PLATFORM_EARLY);
}

static void platform_dev_init(uint level) {
    pdev_run_hooks(level);
}

LK_INIT_HOOK(platform_dev_init, platform_dev_init, LK_INIT_LEVEL_PLATFORM);
