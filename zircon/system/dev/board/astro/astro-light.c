// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include <soc/aml-s905d2/s905d2-gpio.h>

#include "astro.h"
#include "astro-gpios.h"

// Composite binding rules for focaltech touch driver.
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
const zx_bind_inst_t i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, ASTRO_I2C_A0_0),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, I2C_AMBIENTLIGHT_ADDR),
};
static const zx_bind_inst_t gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_LIGHT_INTERRUPT),
};
static const device_component_part_t i2c_component[] = {
    { countof(root_match), root_match },
    { countof(i2c_match), i2c_match },
};
static const device_component_part_t gpio_component[] = {
    { countof(root_match), root_match },
    { countof(gpio_match), gpio_match },
};
static const device_component_t components[] = {
    { countof(i2c_component), i2c_component },
    { countof(gpio_component), gpio_component },
};

zx_status_t ams_light_init(aml_bus_t* bus) {
    const zx_device_prop_t props[] = {
        { BIND_PLATFORM_DEV_VID, 0, PDEV_VID_AMS },
        { BIND_PLATFORM_DEV_PID, 0, PDEV_PID_AMS_TCS3400 },
        { BIND_PLATFORM_DEV_DID, 0, PDEV_DID_AMS_LIGHT },
    };

    zx_status_t status = device_add_composite(bus->parent, "tcs3400-light", props, countof(props),
                                              components, countof(components), UINT32_MAX);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ams_light_init(tcs-3400): device_add_composite failed: %d\n", status);
        return status;
    }

    return ZX_OK;
}
