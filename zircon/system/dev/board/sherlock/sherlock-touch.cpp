// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include <lib/focaltech/focaltech.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>
#include <limits.h>
#include <unistd.h>

#include "sherlock.h"

namespace sherlock {

static const pbus_gpio_t touch_gpios[] = {
    {
        // touch interrupt
        .gpio = T931_GPIOZ(1),
    },
    {
        // touch reset
        .gpio = T931_GPIOZ(9),
    },
};

static const pbus_i2c_channel_t ft5726_touch_i2c[] = {
    {
        .bus_id = SHERLOCK_I2C_2,
        .address = 0x38,
    },
};

static const uint32_t device_id = FOCALTECH_DEVICE_FT5726;
static const pbus_metadata_t ft5726_touch_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = &device_id,
        .data_size = sizeof(device_id)
    },
};

const pbus_dev_t ft5726_touch_dev = []() {
    pbus_dev_t dev;
    dev.name = "ft5726-touch";
    dev.vid = PDEV_VID_GENERIC;
    dev.pid = PDEV_PID_SHERLOCK;
    dev.did = PDEV_DID_FOCALTOUCH;
    dev.i2c_channel_list = ft5726_touch_i2c;
    dev.i2c_channel_count = countof(ft5726_touch_i2c);
    dev.gpio_list = touch_gpios;
    dev.gpio_count = countof(touch_gpios);
    dev.metadata_list = ft5726_touch_metadata;
    dev.metadata_count = countof(ft5726_touch_metadata);
    return dev;
}();


zx_status_t Sherlock::TouchInit() {
    zx_status_t status = pbus_.DeviceAdd(&ft5726_touch_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s(ft5726): DeviceAdd failed: %d\n", __func__, status);
        return status;
    }
    return ZX_OK;
}

} // namespace sherlock
