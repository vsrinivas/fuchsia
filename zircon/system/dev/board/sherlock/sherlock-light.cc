// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <zircon/compiler.h>

#include "sherlock.h"
#include "sherlock-gpios.h"

namespace sherlock {

zx_status_t Sherlock::LightInit() {

    constexpr zx_bind_inst_t root_match[] = {
        BI_MATCH(),
    };
    constexpr zx_bind_inst_t gpio_match[] = {
        BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
        BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_LIGHT_INTERRUPT),
    };
    constexpr zx_bind_inst_t i2c_match[] = {
        BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
        BI_ABORT_IF(NE, BIND_I2C_BUS_ID, SHERLOCK_I2C_A0_0),
        BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x39),
    };
    const device_component_part_t gpio_component[] = {
        { countof(root_match), root_match },
        { countof(gpio_match), gpio_match },
    };
    const device_component_part_t i2c_component[] = {
        { countof(root_match), root_match },
        { countof(i2c_match), i2c_match },
    };
    const device_component_t components[] = {
        { countof(i2c_component), i2c_component },
        { countof(gpio_component), gpio_component },
    };

    constexpr zx_device_prop_t props[] = {
        { BIND_PLATFORM_DEV_VID, 0, PDEV_VID_AMS },
        { BIND_PLATFORM_DEV_PID, 0, PDEV_PID_AMS_TCS3400 },
        { BIND_PLATFORM_DEV_DID, 0, PDEV_DID_AMS_LIGHT },
    };

    auto status = DdkAddComposite("SherlockLightSensor", props, countof(props), components,
                                  countof(components), UINT32_MAX);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s DdkAddComposite failed %d\n", __FUNCTION__, status);
        return status;
    }
    return ZX_OK;
}

} // namespace sherlock
