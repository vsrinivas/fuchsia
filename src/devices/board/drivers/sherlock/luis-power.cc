// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/i2c.h>
#include <ddk/metadata/power.h>
#include <ddk/platform-defs.h>
#include <soc/aml-common/aml-power.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-power.h>
#include <soc/aml-t931/t931-pwm.h>

#include "sherlock.h"

namespace sherlock {

constexpr zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };

zx_status_t Sherlock::LuisPowerPublishBuck(const char* name, uint32_t bus_id, uint16_t address) {
  const zx_bind_inst_t i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, bus_id),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, address),
  };

  const device_fragment_part_t i2c_fragment[] = {
    {countof(root_match), root_match},
    {countof(i2c_match), i2c_match},
  };

  const device_fragment_t fragments[] = {
    {countof(i2c_fragment), i2c_fragment},
  };

  const zx_device_prop_t props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_SILERGY},
    {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_SILERGY_SYBUCK},
  };

  const i2c_channel_t i2c_channels = {
      .bus_id = bus_id,
      .address = address,
  };

  const device_metadata_t metadata[] = {
    {
      .type = DEVICE_METADATA_I2C_CHANNELS,
      .data = &i2c_channels,
      .length = sizeof(i2c_channels),
    },
  };

  const composite_device_desc_t comp_desc = {
    .props = props,
    .props_count = countof(props),
    .fragments = fragments,
    .fragments_count = countof(fragments),
    .coresident_device_index = 0,
    .metadata_list = metadata,
    .metadata_count = countof(metadata),
  };

  return DdkAddComposite(name, &comp_desc);
}

zx_status_t Sherlock::LuisPowerInit() {
  zx_status_t st;

  st = LuisPowerPublishBuck("0p8_ee_buck", SHERLOCK_I2C_A0_0, 0x60);
  if (st != ZX_OK) {
    zxlogf(ERROR, "Failed to publish sy8827 0P8_EE_BUCK device, st = %d", st);
    return st;
  }

  st = LuisPowerPublishBuck("cpu_a_buck", SHERLOCK_I2C_3, 0x60);
  if (st != ZX_OK) {
    zxlogf(ERROR, "Failed to publish sy8827 CPU_A_BUCK device, st = %d", st);
    return st;
  }

  return ZX_OK;
}

}  // namespace sherlock
