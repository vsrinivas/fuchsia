// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

constexpr pbus_mmio_t sherlock_canvas_mmios[] = {
    {
        .base = T931_DMC_BASE,
        .length = T931_DMC_LENGTH,
    },
};

constexpr pbus_bti_t sherlock_canvas_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_CANVAS,
    },
};

constexpr pbus_dev_t canvas_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "canvas";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_AMLOGIC_CANVAS;
  dev.mmio_list = sherlock_canvas_mmios;
  dev.mmio_count = countof(sherlock_canvas_mmios);
  dev.bti_list = sherlock_canvas_btis;
  dev.bti_count = countof(sherlock_canvas_btis);
  return dev;
}();

zx_status_t Sherlock::CanvasInit() {
  zx_status_t status = pbus_.DeviceAdd(&canvas_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Sherlock::CanvasInit: DeviceAdd failed: %d", status);
    return status;
  }
  return status;
}

}  // namespace sherlock
