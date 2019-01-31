// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <pdev/pdev.h>

#include <assert.h>
#include <lk/init.h>
#include <pdev/driver.h>
#include <stdio.h>

static const zbi_header_t* driver_zbi = NULL;

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
    if (!driver_zbi) {
        return;
    }

    const zbi_header_t* item = driver_zbi;
    DEBUG_ASSERT(item->type == ZBI_TYPE_CONTAINER);
    DEBUG_ASSERT(item->extra == ZBI_CONTAINER_MAGIC);

    const uint8_t* start = (uint8_t*)item + sizeof(zbi_header_t);
    const uint8_t* end = start + item->length;

    while (static_cast<size_t>(end - start) > sizeof(zbi_header_t)) {
        item = reinterpret_cast<const zbi_header_t*>(start);
        if (item->type == ZBI_TYPE_KERNEL_DRIVER) {
            // kernel driver type is in boot item extra
            // driver data follows boot item
            pdev_init_driver(item->extra, &item[1], item->length, level);
        }
        start += ZBI_ALIGN((uint32_t)sizeof(zbi_header_t) + item->length);
    }
}

void pdev_init(const zbi_header_t* zbi) {
    ASSERT(zbi);
    driver_zbi = zbi;

    pdev_run_hooks(LK_INIT_LEVEL_PLATFORM_EARLY);
}

static void platform_dev_init(uint level) {
    pdev_run_hooks(level);
}

LK_INIT_HOOK(platform_dev_init, platform_dev_init, LK_INIT_LEVEL_PLATFORM);
