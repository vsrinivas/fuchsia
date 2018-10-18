// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backlight.h"
#include <ddk/debug.h>
#include <ddk/protocol/i2c-lib.h>
#include <ddk/protocol/platform-device.h>

namespace astro_display {

// Table from Linux source
// TODO(ZX-2455): Need to separate backlight driver from display driver
struct I2cCommand {
    uint8_t reg;
    uint8_t val;
};

namespace {
constexpr I2cCommand kBacklightInitTable[] = {
    {0xa2, 0x20},
    {0xa5, 0x54},
    {0x00, 0xff},
    {0x01, 0x05},
    {0xa2, 0x20},
    {0xa5, 0x54},
    {0xa1, 0xb7},
    {0xa0, 0xff},
    {0x00, 0x80},
};
} // namespace

zx_status_t Backlight::Init(zx_device_t* parent) {
    if (initialized_) {
        return ZX_OK;
    }

    platform_device_protocol_t pdev;
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &pdev);
    if (status != ZX_OK) {
        DISP_ERROR("Could not obtain platform device protocol\n");
        return status;
    }

    // Obtain I2C Protocol for backlight
    status = device_get_protocol(parent, ZX_PROTOCOL_I2C, &i2c_);
    if (status != ZX_OK) {
        DISP_ERROR("Could not obtain I2C protocol\n");
        return status;
    }

    // Obtain GPIO Protocol for backlight enable
    status = pdev_get_protocol(&pdev, ZX_PROTOCOL_GPIO, GPIO_BL, &gpio_);
    if (status != ZX_OK) {
        DISP_ERROR("Could not obtain GPIO protocol\n");
        return status;
    }

    // set gpio pin as output
    gpio_config_out(&gpio_, 1);
    zx_nanosleep(zx_deadline_after(ZX_USEC(10))); // optional small delay for pin to settle
    initialized_ = true;
    return ZX_OK;
}

void Backlight::Enable() {
    ZX_DEBUG_ASSERT(initialized_);
    if (enabled_) {
        return;
    }
    // power on backlight
    gpio_write(&gpio_, 1);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(1))); // delay to ensure backlight is powered on

    for (size_t i = 0; i < fbl::count_of(kBacklightInitTable); i++) {
        if (i2c_write_sync(&i2c_, &kBacklightInitTable[i], 2) != ZX_OK) {
            DISP_ERROR("Backlight write failed: reg[0x%x]: 0x%x\n", kBacklightInitTable[i].reg,
                       kBacklightInitTable[i].val);
        }
    }
    enabled_ = true;
    return;
}

void Backlight::Disable() {
    ZX_DEBUG_ASSERT(initialized_);
    if (!enabled_) {
        return;
    }
    // power off backlight
    gpio_write(&gpio_, 0);
    enabled_ = false;
}

} // namespace astro_display
