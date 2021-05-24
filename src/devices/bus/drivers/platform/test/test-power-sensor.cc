// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <lib/ddk/metadata.h>

#include "test.h"

namespace board_test {

zx_status_t TestBoard::PowerSensorInit() {
  pbus_dev_t power_sensor_dev = {};
  power_sensor_dev.name = "power-sensor";
  power_sensor_dev.vid = PDEV_VID_TEST;
  power_sensor_dev.pid = PDEV_PID_PBUS_TEST;
  power_sensor_dev.did = PDEV_DID_TEST_POWER_SENSOR;

  zx_status_t status = pbus_.DeviceAdd(&power_sensor_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_test
