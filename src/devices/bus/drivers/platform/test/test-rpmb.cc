// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>

#include "test.h"

namespace board_test {

zx_status_t TestBoard::RpmbInit() {
  pbus_dev_t rpmb_dev = {};
  rpmb_dev.name = "rpmb";
  rpmb_dev.vid = PDEV_VID_TEST;
  rpmb_dev.pid = PDEV_PID_PBUS_TEST;
  rpmb_dev.did = PDEV_DID_TEST_RPMB;

  zx_status_t status = pbus_.DeviceAdd(&rpmb_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_test
