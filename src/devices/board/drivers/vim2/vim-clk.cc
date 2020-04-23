// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/clock.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-meson/axg-clk.h>
#include <soc/aml-s912/s912-hw.h>

#include "vim.h"

namespace vim {
static const pbus_mmio_t clk_mmios[] = {
    {
        .base = S912_HIU_BASE,
        .length = S912_HIU_LENGTH,
    },
    {
        .base = S912_DOS_BASE,
        .length = S912_DOS_LENGTH,
    },
};

constexpr clock_id_t clock_ids[] = {
    // For video decoder
    {axg_clk::CLK_DOS_GCLK_VDEC},
};

constexpr pbus_metadata_t clock_metadata[] = {
    {
        .type = DEVICE_METADATA_CLOCK_IDS,
        .data_buffer = &clock_ids,
        .data_size = sizeof(clock_ids),
    },
};

zx_status_t Vim::ClkInit() {
  pbus_dev_t clk_dev = {};
  clk_dev.name = "vim-clk";
  clk_dev.vid = PDEV_VID_AMLOGIC;
  clk_dev.pid = PDEV_PID_AMLOGIC_S912;
  // Technically VIM2 is GXM, but AXG clock values are similar.
  clk_dev.did = PDEV_DID_AMLOGIC_AXG_CLK;
  clk_dev.mmio_list = clk_mmios;
  clk_dev.mmio_count = countof(clk_mmios);
  clk_dev.metadata_list = clock_metadata;
  clk_dev.metadata_count = countof(clock_metadata);

  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_CLOCK_IMPL, &clk_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ClkInit: DeviceAdd failed, st = %d\n", status);
    return status;
  }

  return ZX_OK;
}
}  // namespace vim
