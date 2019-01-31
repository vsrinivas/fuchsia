// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include <limits.h>

#include "vim.h"

namespace vim {

static const pbus_i2c_channel_t pcf8563_rtc_i2c[] = {
    {
        .bus_id = 1,
        .address = 0x51,
    },
};

zx_status_t Vim::RtcInit() {
    pbus_dev_t pcf8563_rtc_dev = {};

    pcf8563_rtc_dev.name = "pcf8563-rtc";
    pcf8563_rtc_dev.vid = PDEV_VID_NXP;
    pcf8563_rtc_dev.pid = PDEV_PID_GENERIC;
    pcf8563_rtc_dev.did = PDEV_DID_PCF8563_RTC;
    pcf8563_rtc_dev.i2c_channel_list = pcf8563_rtc_i2c;
    pcf8563_rtc_dev.i2c_channel_count = countof(pcf8563_rtc_i2c);

    zx_status_t status = pbus_.DeviceAdd(&pcf8563_rtc_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "RtcInit: pbus_device_add failed: %d\n", status);
    }

    return status;
}
} //namespace vim