// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>

#include "test.h"

namespace board_test {

zx_status_t TestBoard::VregInit() {
  pbus_dev_t vreg_dev = {};
  vreg_dev.name = "vreg";
  vreg_dev.vid = PDEV_VID_TEST;
  vreg_dev.pid = PDEV_PID_PBUS_TEST;
  vreg_dev.did = PDEV_DID_TEST_VREG;

  zx_status_t status = pbus_.DeviceAdd(&vreg_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_test
