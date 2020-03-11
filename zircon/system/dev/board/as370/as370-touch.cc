// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddktl/metadata/touch-buttons.h>
#include <fbl/algorithm.h>

#include "as370.h"

namespace board_as370 {

constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t ref_out_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x37),
};
static const device_fragment_part_t ref_out_i2c_fragment[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(ref_out_i2c_match), ref_out_i2c_match},
};

static const zx_bind_inst_t ref_out_touch_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, 5),
};
static const device_fragment_part_t ref_out_touch_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(ref_out_touch_gpio_match), ref_out_touch_gpio_match},
};

static const device_fragment_t controller_fragments[] = {
    {countof(ref_out_i2c_fragment), ref_out_i2c_fragment},
    {countof(ref_out_touch_gpio_fragment), ref_out_touch_gpio_fragment},
};

zx_status_t As370::TouchInit() {
  static constexpr touch_button_config_t as370_touch_buttons[] = {
      {
          .id = BUTTONS_ID_VOLUME_UP,
          .idx = 4,
      },
      {
          .id = BUTTONS_ID_VOLUME_DOWN,
          .idx = 5,
      },
      {
          .id = BUTTONS_ID_PLAY_PAUSE,
          .idx = 0,
      },
  };

  static constexpr device_metadata_t as370_touch_metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data = &as370_touch_buttons,
          .length = sizeof(as370_touch_buttons),
      },
  };

  constexpr zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_SYNAPTICS},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_AS370_TOUCH},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = countof(props),
      .fragments = controller_fragments,
      .fragments_count = countof(controller_fragments),
      .coresident_device_index = UINT32_MAX,
      .metadata_list = as370_touch_metadata,
      .metadata_count = countof(as370_touch_metadata),
  };

  zx_status_t status = DdkAddComposite("as370-touch", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s CompositeDeviceAdd failed %d\n", __FILE__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_as370
