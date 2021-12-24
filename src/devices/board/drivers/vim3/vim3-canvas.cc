// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a311d/a311d-hw.h>

#include "vim3.h"

namespace vim3 {

static const pbus_mmio_t canvas_mmios[] = {
    {
        .base = A311D_DMC_BASE,
        .length = A311D_DMC_LENGTH,
    },
};

static const pbus_bti_t canvas_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_CANVAS,
    },
};

static const pbus_dev_t canvas_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "canvas";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_AMLOGIC_CANVAS;
  dev.mmio_list = canvas_mmios;
  dev.mmio_count = std::size(canvas_mmios);
  dev.bti_list = canvas_btis;
  dev.bti_count = std::size(canvas_btis);
  return dev;
}();

zx_status_t Vim3::CanvasInit() {
  zx_status_t status = pbus_.DeviceAdd(&canvas_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "CanvasInit: DeviceAdd Canvas failed: %d", status);
    return status;
  }
  return ZX_OK;
}

}  // namespace vim3
