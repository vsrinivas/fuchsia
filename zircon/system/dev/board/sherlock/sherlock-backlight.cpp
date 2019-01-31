// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include "sherlock.h"

namespace sherlock {

namespace {
constexpr pbus_i2c_channel_t backlight_i2c_channels[] = {
    {
        .bus_id = SHERLOCK_I2C_3,
        .address = 0x2C,
    },
};

static pbus_dev_t backlight_dev = []() {
    pbus_dev_t dev;
    dev.name = "backlight";
    dev.vid = PDEV_VID_TI;
    dev.pid = PDEV_PID_TI_LP8556;
    dev.did = PDEV_DID_TI_BACKLIGHT;
    dev.i2c_channel_list = backlight_i2c_channels;
    dev.i2c_channel_count = countof(backlight_i2c_channels);
    return dev;
}();

} // namespace

zx_status_t Sherlock::BacklightInit() {

    zx_status_t status = pbus_.DeviceAdd(&backlight_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __func__, status);
        return status;
    }

    return status;
}

} // namespace sherlock