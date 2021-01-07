// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <soc/aml-s912/s912-hw.h>

#include "vim.h"

namespace vim {
static const pbus_mmio_t vim_canvas_mmios[] = {
    {
        .base = S912_DMC_REG_BASE,
        .length = S912_DMC_REG_LENGTH,
    },
};

static const pbus_bti_t vim_canvas_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_CANVAS,
    },
};

zx_status_t Vim::CanvasInit() {
  pbus_dev_t canvas_dev = {};
  canvas_dev.name = "canvas";
  canvas_dev.vid = PDEV_VID_AMLOGIC;
  canvas_dev.pid = PDEV_PID_GENERIC;
  canvas_dev.did = PDEV_DID_AMLOGIC_CANVAS;
  canvas_dev.mmio_list = vim_canvas_mmios;
  canvas_dev.mmio_count = countof(vim_canvas_mmios);
  canvas_dev.bti_list = vim_canvas_btis;
  canvas_dev.bti_count = countof(vim_canvas_btis);

  zx_status_t status = pbus_.DeviceAdd(&canvas_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "CanvasInit: DeviceAdd Canvas failed: %d", status);
    return status;
  }
  return ZX_OK;
}

}  // namespace vim
