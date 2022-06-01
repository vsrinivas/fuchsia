// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/clock.h>
#include <soc/aml-a5/a5-hw.h>
#include <soc/aml-meson/a5-clk.h>

#include "av400.h"

namespace av400 {

constexpr pbus_mmio_t clk_mmios[] = {
    // CLK Registers
    {
        .base = A5_CLK_BASE,
        .length = A5_CLK_LENGTH,
    },
    // CLK MSR block
    {
        .base = A5_MSR_CLK_BASE,
        .length = A5_MSR_CLK_LENGTH,
    },
};

constexpr clock_id_t clock_ids[] = {
    {a5_clk::CLK_ADC},  // PLACEHOLDER.
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
  dev.name = "av400-clk";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_A5;
  dev.did = PDEV_DID_AMLOGIC_A5_CLK;
  dev.mmio_list = clk_mmios;
  dev.mmio_count = std::size(clk_mmios);
  dev.metadata_list = clock_metadata;
  dev.metadata_count = std::size(clock_metadata);
  return dev;
}();

zx_status_t Av400::ClkInit() {
  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_CLOCK_IMPL, &clk_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed, st = %s", __func__, zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

}  // namespace av400
