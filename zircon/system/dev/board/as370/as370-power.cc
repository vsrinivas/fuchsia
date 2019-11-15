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

  static const device_metadata_t power_metadata[] = {
      {
          .type = DEVICE_METADATA_POWER_DOMAINS,
          .data = &power_domains,
          .length = sizeof(power_domains),
      },
  };

  constexpr zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_SYNAPTICS},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_AS370_POWER},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = countof(props),
      .components = components,
      .components_count = countof(components),
      .coresident_device_index = UINT32_MAX,
      .metadata_list = power_metadata,
      .metadata_count = countof(power_metadata),
  };

  status = DdkAddComposite("power", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed %d\n", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_as370
