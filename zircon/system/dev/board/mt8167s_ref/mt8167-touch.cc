// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <fbl/algorithm.h>
#include <lib/focaltech/focaltech.h>
#include <soc/mt8167/mt8167-hw.h>
#include <soc/mt8167/mt8167-power.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::TouchInit() {
    if (board_info_.vid != PDEV_VID_GOOGLE || board_info_.pid != PDEV_PID_CLEO) {
        return ZX_OK;
    }

    static constexpr uint32_t kDeviceId = FOCALTECH_DEVICE_FT6336;

    static const pbus_metadata_t touch_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = &kDeviceId,
            .data_size = sizeof(kDeviceId)
        },
    };

    pbus_dev_t touch_dev = {};
    touch_dev.name = "touch";
    touch_dev.vid = PDEV_VID_GENERIC;
    touch_dev.did = PDEV_DID_FOCALTOUCH;
    touch_dev.metadata_list = touch_metadata;
    touch_dev.metadata_count = countof(touch_metadata);

    // Composite binding rules for focaltech touch driver.
    constexpr zx_bind_inst_t root_match[] = {
        BI_MATCH(),
    };
    constexpr zx_bind_inst_t ft_i2c_match[] = {
        BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
        BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0),
        BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x38),
    };
    constexpr zx_bind_inst_t gpio_int_match[] = {
        BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
        BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO_TOUCH_INT),
    };
    constexpr zx_bind_inst_t gpio_reset_match[] = {
        BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
        BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO_TOUCH_RST),
    };
    const device_component_part_t ft_i2c_component[] = {
        { fbl::count_of(root_match), root_match },
        { fbl::count_of(ft_i2c_match), ft_i2c_match },
    };
    const device_component_part_t gpio_int_component[] = {
        { fbl::count_of(root_match), root_match },
        { fbl::count_of(gpio_int_match), gpio_int_match },
    };
    const device_component_part_t gpio_reset_component[] = {
        { fbl::count_of(root_match), root_match },
        { fbl::count_of(gpio_reset_match), gpio_reset_match },
    };
    const device_component_t ft_components[] = {
        { fbl::count_of(ft_i2c_component), ft_i2c_component },
        { fbl::count_of(gpio_int_component), gpio_int_component },
        { fbl::count_of(gpio_reset_component), gpio_reset_component },
    };

    // platform device protocol is only needed to provide metadata to the driver.
    // TODO(voydanoff) remove pdev after we have a better way to provide metadata to composite
    // devices.
    zx_status_t status = pbus_.CompositeDeviceAdd(&touch_dev, ft_components,
                                                  fbl::count_of(ft_components), UINT32_MAX);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to add touch device: %d\n", __FUNCTION__, status);
    }

    return status;
}

}  // namespace board_mt8167
