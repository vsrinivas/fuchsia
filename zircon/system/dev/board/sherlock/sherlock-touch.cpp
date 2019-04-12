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
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>
#include <limits.h>
#include <unistd.h>

#include "sherlock.h"
#include "sherlock-gpios.h"

namespace sherlock {

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
    dev.metadata_list = ft5726_touch_metadata;
    dev.metadata_count = fbl::count_of(ft5726_touch_metadata);
    return dev;
}();

// Composite binding rules for focaltech touch driver.
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static constexpr zx_bind_inst_t ft_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, SHERLOCK_I2C_2),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x38),
};
static constexpr zx_bind_inst_t gpio_int_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_TOUCH_INTERRUPT),
};
static constexpr zx_bind_inst_t gpio_reset_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_TOUCH_RESET),
};
static constexpr device_component_part_t ft_i2c_component[] = {
    { fbl::count_of(root_match), root_match },
    { fbl::count_of(ft_i2c_match), ft_i2c_match },
};
static constexpr device_component_part_t gpio_int_component[] = {
    { fbl::count_of(root_match), root_match },
    { fbl::count_of(gpio_int_match), gpio_int_match },
};
static constexpr device_component_part_t gpio_reset_component[] = {
    { fbl::count_of(root_match), root_match },
    { fbl::count_of(gpio_reset_match), gpio_reset_match },
};
static constexpr device_component_t ft_components[] = {
    { fbl::count_of(ft_i2c_component), ft_i2c_component },
    { fbl::count_of(gpio_int_component), gpio_int_component },
    { fbl::count_of(gpio_reset_component), gpio_reset_component },
};

zx_status_t Sherlock::TouchInit() {
    // platform device protocol is only needed to provide metadata to the driver.
    // TODO(voydanoff) remove pdev after we have a better way to provide metadata to composite
    // devices.
    zx_status_t status = pbus_.CompositeDeviceAdd(&ft5726_touch_dev, ft_components,
                                                  fbl::count_of(ft_components), UINT32_MAX);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s(ft5726): DeviceAdd failed: %d\n", __func__, status);
        return status;
    }
    return ZX_OK;
}

} // namespace sherlock
