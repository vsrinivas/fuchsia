// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "sherlock.h"

namespace sherlock {

namespace {

constexpr pbus_mmio_t dsi_mmios[] = {
    {
        // DSI Host Controller
        .base = T931_MIPI_DSI_BASE,
        .length = T931_MIPI_DSI_LENGTH,
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

zx_status_t Sherlock::DsiInit() {
  auto status = pbus_.DeviceAdd(&dsi_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock
