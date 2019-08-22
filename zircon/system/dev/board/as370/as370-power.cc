// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/power.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/as370/as370-power.h>

#include "as370.h"

namespace board_as370 {

zx_status_t As370::PowerInit() {
  zx_status_t status;

  static const zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };

  static const zx_bind_inst_t i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0x0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x66),
  };

  static const device_component_part_t i2c_component[] = {
      {countof(root_match), root_match},
      {countof(i2c_match), i2c_match},
  };

  static const device_component_t components[] = {
      {countof(i2c_component), i2c_component},
  };

  static const power_domain_t power_domains[] = {{kBuckSoC}};

  static const pbus_metadata_t power_metadata[] = {
      {
          .type = DEVICE_METADATA_POWER_DOMAINS,
          .data_buffer = &power_domains,
          .data_size = sizeof(power_domains),
      },
  };

  pbus_dev_t power_dev = {};
  power_dev.name = "power";
  power_dev.vid = PDEV_VID_SYNAPTICS;
  power_dev.did = PDEV_DID_AS370_POWER;
  power_dev.metadata_list = power_metadata;
  power_dev.metadata_count = countof(power_metadata);

  status = pbus_.CompositeDeviceAdd(&power_dev, components, countof(components), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed %d\n", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_as370
