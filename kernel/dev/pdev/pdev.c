// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <stdio.h>
#include <pdev/driver.h>
#include <pdev/pdev.h>
#include <lk/init.h>

static const bootdata_t* driver_bootdata = NULL;

extern const struct lk_pdev_init_struct __start_lk_pdev_init[];
extern const struct lk_pdev_init_struct __stop_lk_pdev_init[];

static void pdev_init_driver(uint32_t type, const void* driver_data, uint32_t length, uint level) {
    const struct lk_pdev_init_struct *ptr;
    for (ptr = __start_lk_pdev_init; ptr != __stop_lk_pdev_init; ptr++) {
        if (ptr->type == type && ptr->level == level) {
            ptr->hook(driver_data, length);
            return;
        }
    }
}

static void pdev_run_hooks(uint level) {
    if (!driver_bootdata) return;

    const bootdata_t* bootdata = driver_bootdata;
    DEBUG_ASSERT(bootdata->type == BOOTDATA_CONTAINER);
    DEBUG_ASSERT(bootdata->extra == BOOTDATA_MAGIC);

    const uint8_t* start = (uint8_t*)bootdata + sizeof(bootdata_t);
    const uint8_t* end = start + bootdata->length;

    while ((uint32_t)(end - start) > sizeof(bootdata_t)) {
        bootdata = (const bootdata_t*)start;
        if (bootdata->type == BOOTDATA_KERNEL_DRIVER) {
            // kernel driver type is in bootdata extra
            // driver data follows bootdata
            pdev_init_driver(bootdata->extra, &bootdata[1], bootdata->length, level);
        }
        start += BOOTDATA_ALIGN(sizeof(bootdata_t) + bootdata->length);
    }
}

void pdev_init(const bootdata_t* bootdata) {
    ASSERT(bootdata);
    driver_bootdata = bootdata;

    pdev_run_hooks(LK_INIT_LEVEL_PLATFORM_EARLY);
}

static void platform_dev_init(uint level) {
    pdev_run_hooks(level);
}

LK_INIT_HOOK(platform_dev_init, platform_dev_init, LK_INIT_LEVEL_PLATFORM);
