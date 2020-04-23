// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "nelson.h"

namespace nelson {

static const pbus_mmio_t nelson_canvas_mmios[] = {
    {
        .base = S905D2_DMC_BASE,
        .length = S905D2_DMC_LENGTH,
    },
};

static const pbus_bti_t nelson_canvas_btis[] = {
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
  dev.mmio_list = nelson_canvas_mmios;
  dev.mmio_count = countof(nelson_canvas_mmios);
  dev.bti_list = nelson_canvas_btis;
  dev.bti_count = countof(nelson_canvas_btis);
  return dev;
}();

zx_status_t Nelson::CanvasInit() {
  zx_status_t status = pbus_.DeviceAdd(&canvas_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d\n", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace nelson
