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

static const zx_device_prop_t ft5726_props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
    {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_SHERLOCK},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_FOCALTOUCH},
};

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
static constexpr device_fragment_part_t ft_i2c_fragment[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(ft_i2c_match), ft_i2c_match},
};
static constexpr device_fragment_part_t gpio_int_fragment[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(gpio_int_match), gpio_int_match},
};
static constexpr device_fragment_part_t gpio_reset_fragment[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(gpio_reset_match), gpio_reset_match},
};
static constexpr device_fragment_t ft_fragments[] = {
    {fbl::count_of(ft_i2c_fragment), ft_i2c_fragment},
    {fbl::count_of(gpio_int_fragment), gpio_int_fragment},
    {fbl::count_of(gpio_reset_fragment), gpio_reset_fragment},
};

static const composite_device_desc_t ft_comp_desc = {
    .props = ft5726_props,
    .props_count = countof(ft5726_props),
    .fragments = ft_fragments,
    .fragments_count = countof(ft_fragments),
    .coresident_device_index = UINT32_MAX,
    .metadata_list = ft5726_touch_metadata,
    .metadata_count = fbl::count_of(ft5726_touch_metadata),
};

zx_status_t Sherlock::TouchInit() {
  zx_status_t status = DdkAddComposite("ft5726-touch", &ft_comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s(ft5726): DeviceAdd failed: %d\n", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock
