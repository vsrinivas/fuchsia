// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <lib/ddk/metadata.h>
#include <ddk/metadata/clock.h>
#include <soc/as370/as370-clk.h>
#include <soc/as370/as370-gpio.h>
#include <soc/as370/as370-hw.h>

#include "as370.h"

namespace board_as370 {

zx_status_t As370::ClockInit() {
  static const pbus_mmio_t clock_mmios[] = {
      {
          .base = as370::kGlobalBase,
          .length = as370::kGlobalSize,
      },
      {
          .base = as370::kAudioGlobalBase,
          .length = as370::kAudioGlobalSize,
      },
      {
          .base = as370::kCpuBase,
          .length = as370::kCpuSize,
      },
  };
  static const clock_id_t clock_ids[] = {
      {as370::As370Clk::kClkAvpll0},
      {as370::As370Clk::kClkAvpll1},
      {as370::As370Clk::kClkCpu},
  };
  static const pbus_metadata_t clock_metadata[] = {
      {
          .type = DEVICE_METADATA_CLOCK_IDS,
          .data_buffer = reinterpret_cast<const uint8_t*>(&clock_ids),
          .data_size = sizeof(clock_ids),
      },
  };

  pbus_dev_t dev = {};
  dev.name = "as370-clock";
  dev.vid = PDEV_VID_SYNAPTICS;
  dev.did = PDEV_DID_AS370_CLOCK;
  dev.mmio_list = clock_mmios;
  dev.mmio_count = countof(clock_mmios);
  dev.metadata_list = clock_metadata;
  dev.metadata_count = countof(clock_metadata);

  auto status = pbus_.DeviceAdd(&dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_as370
