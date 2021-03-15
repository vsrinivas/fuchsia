// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/platform-defs.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

namespace astro {

namespace {

constexpr pbus_mmio_t dsi_mmios[] = {
    {
        // DSI Host Controller
        .base = S905D2_MIPI_DSI_BASE,
        .length = S905D2_MIPI_DSI_LENGTH,
    },
};

pbus_dev_t dsi_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "dw-dsi";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_DW_DSI;
  dev.mmio_list = dsi_mmios;
  dev.mmio_count = countof(dsi_mmios);
  return dev;
}();

}  // namespace

zx_status_t Astro::DsiInit() {
  auto status = pbus_.DeviceAdd(&dsi_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace astro
