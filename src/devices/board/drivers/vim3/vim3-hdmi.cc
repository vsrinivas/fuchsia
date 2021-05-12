// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "vim3.h"

namespace vim3 {

static constexpr pbus_mmio_t hdmi_mmios[] = {
    {
        // HDMITX
        .base = A311D_HDMITX_BASE,
        .length = A311D_HDMITX_LENGTH,
    },
};

static constexpr pbus_dev_t hdmi_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-hdmi";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_A311D;
  dev.did = PDEV_DID_AMLOGIC_HDMI;
  dev.mmio_list = hdmi_mmios;
  dev.mmio_count = countof(hdmi_mmios);
  return dev;
}();

zx_status_t Vim3::HdmiInit() {
  auto status = pbus_.DeviceAdd(&hdmi_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace vim3
