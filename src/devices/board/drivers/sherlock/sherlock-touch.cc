// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/focaltech/focaltech.h>
#include <limits.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <fbl/algorithm.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock-gpios.h"
#include "sherlock.h"

namespace sherlock {

static const uint32_t device_id = FOCALTECH_DEVICE_FT5726;
static const device_metadata_t ft5726_touch_metadata[] = {
    {.type = DEVICE_METADATA_PRIVATE, .data = &device_id, .length = sizeof(device_id)},
};

static const zx_device_prop_t sherlock_touch_props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
    {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_SHERLOCK},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_FOCALTOUCH},
};

static const zx_device_prop_t luis_touch_props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_FOCALTECH},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_FOCALTECH_FT8201},
};

// Composite binding rules for focaltech touch driver.
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};

static constexpr zx_bind_inst_t sherlock_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, SHERLOCK_I2C_2),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x38),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x40),
};
static constexpr device_fragment_part_t sherlock_i2c_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(sherlock_i2c_match), sherlock_i2c_match},
};

static constexpr zx_bind_inst_t luis_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, SHERLOCK_I2C_2),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x40),
};
static constexpr device_fragment_part_t luis_i2c_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(sherlock_i2c_match), luis_i2c_match},
};

static constexpr zx_bind_inst_t gpio_int_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_TOUCH_INTERRUPT),
};
static constexpr device_fragment_part_t gpio_int_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(gpio_int_match), gpio_int_match},
};

static constexpr zx_bind_inst_t gpio_reset_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_TOUCH_RESET),
};
static constexpr device_fragment_part_t gpio_reset_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(gpio_reset_match), gpio_reset_match},
};

static constexpr device_fragment_t sherlock_fragments[] = {
    {std::size(sherlock_i2c_fragment), sherlock_i2c_fragment},
    {std::size(gpio_int_fragment), gpio_int_fragment},
    {std::size(gpio_reset_fragment), gpio_reset_fragment},
};

static constexpr device_fragment_t luis_fragments[] = {
    {std::size(sherlock_i2c_fragment), luis_i2c_fragment},
    {std::size(gpio_int_fragment), gpio_int_fragment},
    {std::size(gpio_reset_fragment), gpio_reset_fragment},
};

static const composite_device_desc_t sherlock_comp_desc = {
    .props = sherlock_touch_props,
    .props_count = countof(sherlock_touch_props),
    .fragments = sherlock_fragments,
    .fragments_count = countof(sherlock_fragments),
    .coresident_device_index = UINT32_MAX,
    .metadata_list = ft5726_touch_metadata,
    .metadata_count = std::size(ft5726_touch_metadata),
};

static const composite_device_desc_t luis_comp_desc = {
    .props = luis_touch_props,
    .props_count = countof(luis_touch_props),
    .fragments = luis_fragments,
    .fragments_count = countof(luis_fragments),
    .coresident_device_index = UINT32_MAX,
};

zx_status_t Sherlock::TouchInit() {
  zx_status_t status;
  if (pid_ == PDEV_PID_LUIS) {
    status = DdkAddComposite("ft8201-touch", &luis_comp_desc);
  } else {
    status = DdkAddComposite("ft5726-touch", &sherlock_comp_desc);
  }

  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock
