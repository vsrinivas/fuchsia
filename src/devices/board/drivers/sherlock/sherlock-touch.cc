// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/focaltech/focaltech.h>
#include <limits.h>
#include <unistd.h>

#include <fbl/algorithm.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock-gpios.h"
#include "sherlock.h"
#include "src/devices/board/drivers/sherlock/luis-touch-bind.h"
#include "src/devices/board/drivers/sherlock/sherlock-touch-bind.h"

namespace sherlock {

static const zx_device_prop_t sherlock_touch_props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
    {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_SHERLOCK},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_FOCALTOUCH},
};

static const zx_device_prop_t luis_touch_props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_FOCALTECH},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_FOCALTECH_FT8201},
};

static const composite_device_desc_t luis_comp_desc = {
    .props = luis_touch_props,
    .props_count = countof(luis_touch_props),
    .fragments = ft8201_touch_fragments,
    .fragments_count = countof(ft8201_touch_fragments),
    .primary_fragment = "i2c",
    .spawn_colocated = false,
};

zx_status_t Sherlock::TouchInit() {
  zx_status_t status;
  if (pid_ == PDEV_PID_LUIS) {
    status = DdkAddComposite("ft8201-touch", &luis_comp_desc);
  } else {
    static const FocaltechMetadata device_info = {
        .device_id = FOCALTECH_DEVICE_FT5726,
        .needs_firmware = true,
        .display_vendor = GetDisplayVendor(),
        .ddic_version = GetDdicVersion(),
    };
    static const device_metadata_t ft5726_touch_metadata[] = {
        {.type = DEVICE_METADATA_PRIVATE, .data = &device_info, .length = sizeof(device_info)},
    };
    static const composite_device_desc_t sherlock_comp_desc = {
        .props = sherlock_touch_props,
        .props_count = countof(sherlock_touch_props),
        .fragments = ft5726_touch_fragments,
        .fragments_count = countof(ft5726_touch_fragments),
        .primary_fragment = "i2c",
        .spawn_colocated = false,
        .metadata_list = ft5726_touch_metadata,
        .metadata_count = std::size(ft5726_touch_metadata),
    };

    status = DdkAddComposite("ft5726-touch", &sherlock_comp_desc);
  }

  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock
