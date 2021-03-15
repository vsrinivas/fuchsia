// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/platform-defs.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>

#include "nelson.h"

namespace nelson {

constexpr uint32_t kShuntResistorMicroOhms = 10'000;
constexpr device_metadata_t metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data = &kShuntResistorMicroOhms,
        .length = sizeof(kShuntResistorMicroOhms),
    },
};

constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};

constexpr zx_bind_inst_t mlb_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, NELSON_I2C_3),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, I2C_TI_INA231_MLB_ADDR),
};

constexpr device_fragment_part_t mlb_i2c_fragment[] = {
    {countof(root_match), root_match},
    {countof(mlb_i2c_match), mlb_i2c_match},
};

constexpr device_fragment_t mlb_fragments[] = {
    {"i2c", countof(mlb_i2c_fragment), mlb_i2c_fragment},
};

constexpr zx_bind_inst_t speakers_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, NELSON_I2C_3),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, I2C_TI_INA231_SPEAKERS_ADDR),
};

constexpr device_fragment_part_t speakers_i2c_fragment[] = {
    {countof(root_match), root_match},
    {countof(speakers_i2c_match), speakers_i2c_match},
};

constexpr device_fragment_t speakers_fragments[] = {
    {"i2c", countof(speakers_i2c_fragment), speakers_i2c_fragment},
};

constexpr zx_device_prop_t props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TI},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TI_INA231},
};

constexpr composite_device_desc_t mlb_power_sensor_dev = {
    .props = props,
    .props_count = countof(props),
    .fragments = mlb_fragments,
    .fragments_count = countof(mlb_fragments),
    .coresident_device_index = 0,
    .metadata_list = metadata,
    .metadata_count = countof(metadata),
};

constexpr composite_device_desc_t speakers_power_sensor_dev = {
    .props = props,
    .props_count = countof(props),
    .fragments = speakers_fragments,
    .fragments_count = countof(speakers_fragments),
    .coresident_device_index = 0,
    .metadata_list = metadata,
    .metadata_count = countof(metadata),
};

zx_status_t Nelson::PowerInit() {
  zx_status_t status = DdkAddComposite("ti-ina231-mlb", &mlb_power_sensor_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s DdkAddComposite failed %d", __FUNCTION__, status);
    return status;
  }

  if ((status = DdkAddComposite("ti-ina231-speakers", &speakers_power_sensor_dev)) != ZX_OK) {
    zxlogf(ERROR, "%s DdkAddComposite failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace nelson
