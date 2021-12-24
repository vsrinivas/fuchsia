// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/clock.h>
#include <soc/aml-a311d/a311d-hw.h>
#include <soc/aml-meson/g12b-clk.h>

#include "vim3.h"

namespace vim3 {
static const pbus_mmio_t clk_mmios[] = {
    {
        .base = A311D_HIU_BASE,
        .length = A311D_HIU_LENGTH,
    },
    {
        .base = A311D_DOS_BASE,
        .length = A311D_DOS_LENGTH,
    },
    {
        .base = A311D_MSR_CLK_BASE,
        .length = A311D_MSR_CLK_LENGTH,
    },
};

// clang-format off
static const clock_id_t clock_ids[] = {
    {g12b_clk::G12B_CLK_SYS_PLL_DIV16},
    {g12b_clk::G12B_CLK_SYS_CPU_CLK_DIV16},
    {g12b_clk::G12B_CLK_SYS_PLLB_DIV16},
    {g12b_clk::G12B_CLK_SYS_CPUB_CLK_DIV16},
    {g12b_clk::G12B_CLK_DOS_GCLK_VDEC},
    {g12b_clk::G12B_CLK_DOS},
    {g12b_clk::CLK_SYS_CPU_BIG_CLK},
    {g12b_clk::CLK_SYS_CPU_LITTLE_CLK},
};
// clang-format on

static const pbus_metadata_t clock_metadata[] = {
    {
        .type = DEVICE_METADATA_CLOCK_IDS,
        .data_buffer = reinterpret_cast<const uint8_t*>(&clock_ids),
        .data_size = sizeof(clock_ids),
    },
};

static pbus_dev_t clk_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "vim3-clk";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.did = PDEV_DID_AMLOGIC_G12B_CLK;
  dev.mmio_list = clk_mmios;
  dev.mmio_count = std::size(clk_mmios);
  dev.metadata_list = clock_metadata;
  dev.metadata_count = std::size(clock_metadata);
  return dev;
}();

zx_status_t Vim3::ClkInit() {
  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_CLOCK_IMPL, &clk_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ClkInit: DeviceAdd failed, st = %d", status);
    return status;
  }

  clk_impl_ = ddk::ClockImplProtocolClient(parent());
  if (!clk_impl_.is_valid()) {
    zxlogf(ERROR, "%s: ClockImplProtocolClient failed", __func__);
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}
}  // namespace vim3
