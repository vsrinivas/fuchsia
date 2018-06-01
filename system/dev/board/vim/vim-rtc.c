// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>

#include <limits.h>

#include "vim.h"

static const pbus_i2c_channel_t pcf8563_rtc_i2c[] = {
    {
        .bus_id = 1,
        .address = 0x51,
    },
};

static pbus_dev_t pcf8563_rtc_dev = {
    .name = "pcf8563-rtc",
    .vid = PDEV_VID_NXP,
    .pid = PDEV_PID_PCF8563,
    .did = PDEV_DID_PCF8563_RTC,
    .i2c_channels = pcf8563_rtc_i2c,
    .i2c_channel_count = countof(pcf8563_rtc_i2c),
};

zx_status_t vim_rtc_init(vim_bus_t* bus) {

    zx_status_t status = pbus_device_add(&bus->pbus, &pcf8563_rtc_dev, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s(pcf8563): pbus_device_add failed: %d\n", __FUNCTION__, status);
    }

    return status;
}
