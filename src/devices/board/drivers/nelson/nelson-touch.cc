// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include "nelson.h"
#include "src/devices/board/drivers/nelson/gt6853_touch_bind.h"

namespace nelson {

static const pbus_boot_metadata_t touch_boot_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_BOARD_PRIVATE,
        .zbi_extra = 0,
    },
};

zx_status_t Nelson::TouchInit() {
  const uint32_t display_id = GetDisplayId();
  zxlogf(INFO, "Board rev: %u", GetBoardRev());
  zxlogf(INFO, "Panel ID: 0b%d%d", display_id & 0b10 ? 1 : 0, display_id & 0b01 ? 1 : 0);

  const pbus_dev_t touch_dev = {
      .name = "gt6853-touch",
      .vid = PDEV_VID_GOODIX,
      .did = PDEV_DID_GOODIX_GT6853,
      .boot_metadata_list = touch_boot_metadata,
      .boot_metadata_count = std::size(touch_boot_metadata),
  };
  zx_status_t status =
      pbus_.AddComposite(&touch_dev, reinterpret_cast<uint64_t>(gt6853_touch_fragments),
                         std::size(gt6853_touch_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "nelson_touch_init(gt6853): composite_device_add failed: %d", status);
    return status;
  }
  return ZX_OK;
}

}  // namespace nelson
