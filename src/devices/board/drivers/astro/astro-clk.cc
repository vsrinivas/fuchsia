// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <lib/ddk/metadata.h>
#include <ddk/metadata/clock.h>
#include <soc/aml-meson/g12a-clk.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

namespace astro {

constexpr pbus_mmio_t clk_mmios[] = {
    // CLK Registers
    {
        .base = S905D2_HIU_BASE,
        .length = S905D2_HIU_LENGTH,
    },
    {
        .base = S905D2_DOS_BASE,
        .length = S905D2_DOS_LENGTH,
    },
    // CLK MSR block
    {
        .base = S905D2_MSR_CLK_BASE,
        .length = S905D2_MSR_CLK_LENGTH,
    },
};

constexpr clock_id_t clock_ids[] = {
    // For CPU device.
    {g12a_clk::CLK_SYS_PLL_DIV16},
    {g12a_clk::CLK_SYS_CPU_CLK_DIV16},
    {g12a_clk::CLK_SYS_CPU_CLK},
    // For video decoder
    {g12a_clk::CLK_DOS_GCLK_VDEC},
    {g12a_clk::CLK_DOS},
};

const pbus_metadata_t clock_metadata[] = {
    {
        .type = DEVICE_METADATA_CLOCK_IDS,
        .data_buffer = reinterpret_cast<const uint8_t*>(&clock_ids),
        .data_size = sizeof(clock_ids),
    },
};

static const pbus_dev_t clk_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "astro-clk";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_S905D2;
  dev.did = PDEV_DID_AMLOGIC_G12A_CLK;
  dev.mmio_list = clk_mmios;
  dev.mmio_count = countof(clk_mmios);
  dev.metadata_list = clock_metadata;
  dev.metadata_count = countof(clock_metadata);
  return dev;
}();

zx_status_t Astro::ClkInit() {
  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_CLOCK_IMPL, &clk_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed, st = %d", __func__, status);
    return status;
  }

  clk_impl_ = ddk::ClockImplProtocolClient(parent());
  if (!clk_impl_.is_valid()) {
    zxlogf(ERROR, "%s: ClockImplProtocolClient failed", __func__);
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

}  // namespace astro
