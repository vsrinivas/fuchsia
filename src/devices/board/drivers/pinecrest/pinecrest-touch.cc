// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/hardware/gpio/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <limits.h>

#include <ddktl/metadata/touch-buttons.h>
#include <fbl/algorithm.h>

#include "pinecrest.h"
#include "src/devices/board/drivers/pinecrest/pinecrest-bind.h"

namespace board_pinecrest {
namespace fpbus = fuchsia_hardware_platform_bus;

static const zx_bind_inst_t ref_out_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_FIDL_PROTOCOL, ZX_FIDL_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 1),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x37),
};
static const device_fragment_part_t ref_out_i2c_fragment[] = {
    {std::size(ref_out_i2c_match), ref_out_i2c_match},
};

static const zx_bind_inst_t ref_out_touch_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, 5),
};
static const device_fragment_part_t ref_out_touch_gpio_fragment[] = {
    {std::size(ref_out_touch_gpio_match), ref_out_touch_gpio_match},
};

static const device_fragment_t controller_fragments[] = {
    {"i2c", std::size(ref_out_i2c_fragment), ref_out_i2c_fragment},
    {"gpio", std::size(ref_out_touch_gpio_fragment), ref_out_touch_gpio_fragment},
};

zx_status_t Pinecrest::TouchInit() {
  static constexpr touch_button_config_t touch_buttons[] = {
      {
          .id = BUTTONS_ID_VOLUME_UP,
          .idx = 0,
      },
      {
          .id = BUTTONS_ID_VOLUME_DOWN,
          .idx = 5,
      },
      {
          .id = BUTTONS_ID_PLAY_PAUSE,
          .idx = 4,
      },
  };

  static constexpr device_metadata_t touch_metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data = &touch_buttons,
          .length = sizeof(touch_buttons),
      },
  };

  constexpr zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_SYNAPTICS},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_AS370_TOUCH},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = std::size(props),
      .fragments = controller_fragments,
      .fragments_count = std::size(controller_fragments),
      .primary_fragment = "i2c",
      .spawn_colocated = false,
      .metadata_list = touch_metadata,
      .metadata_count = std::size(touch_metadata),
  };

  zx_status_t status = DdkAddComposite("pinecrest-touch", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s CompositeDeviceAdd failed %d", __FILE__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_pinecrest
