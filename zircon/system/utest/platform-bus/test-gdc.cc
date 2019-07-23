// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include "test.h"

namespace board_test {

zx_status_t TestBoard::GdcInit() {
  pbus_dev_t gdc_dev = {};
  gdc_dev.name = "gdc";
  gdc_dev.vid = PDEV_VID_TEST;
  gdc_dev.pid = PDEV_PID_PBUS_TEST;
  gdc_dev.did = PDEV_DID_TEST_GDC;

  zx_status_t status = pbus_.DeviceAdd(&gdc_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __FUNCTION__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace board_test
