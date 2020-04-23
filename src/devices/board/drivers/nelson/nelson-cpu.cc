// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include "nelson.h"

namespace nelson {

static const pbus_dev_t cpu_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "nelson-cpu";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_S905D2;
  dev.did = PDEV_DID_AMLOGIC_CPU;
  return dev;
}();

zx_status_t Nelson::CpuInit() {
  zx_status_t st = pbus_.DeviceAdd(&cpu_dev);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d\n", __func__, st);
    return st;
  }

  return ZX_OK;
}

}  // namespace nelson
