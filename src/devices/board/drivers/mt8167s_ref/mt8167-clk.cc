// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/clock.h>
#include <ddk/platform-defs.h>
#include <soc/mt8167/mt8167-clk.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

constexpr pbus_mmio_t clock_mmios[] = {{.base = MT8167_XO_BASE, .length = MT8167_XO_SIZE}};

constexpr clock_id_t clock_ids[] = {
    // For thermal driver.
    {kClkThem},
    {kClkAuxAdc},
    {kClkPmicwrapAp},
    {kClkPmicwrap26m},
    // For GPU driver.
    {kClkRgSlowMfg},
    {kClkRgAxiMfg},
    {kClkMfgMm},
};

static const pbus_metadata_t clock_metadata[] = {
    {
        .type = DEVICE_METADATA_CLOCK_IDS,
        .data_buffer = &clock_ids,
        .data_size = sizeof(clock_ids),
    },
};

zx_status_t Mt8167::ClkInit() {
  pbus_dev_t clk_dev = {};
  clk_dev.name = "clk";
  clk_dev.vid = PDEV_VID_MEDIATEK;
  clk_dev.did = PDEV_DID_MEDIATEK_CLK;
  clk_dev.mmio_list = clock_mmios;
  clk_dev.mmio_count = countof(clock_mmios);
  clk_dev.metadata_list = clock_metadata;
  clk_dev.metadata_count = countof(clock_metadata);

  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_CLOCK_IMPL, &clk_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd clk failed %d\n", __FUNCTION__, status);
  }

  return status;
}

}  // namespace board_mt8167
