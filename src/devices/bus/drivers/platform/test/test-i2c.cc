// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <lib/ddk/metadata.h>
#include <ddk/metadata/i2c.h>

#include "test.h"

namespace board_test {

namespace {

static const i2c_channel_t i2c_channels[] = {
    {
        .bus_id = 0,
        .address = 16,
        // VID/PID/DID unused.
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    {
        .bus_id = 0,
        .address = 17,
        // VID/PID/DID unused.
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    {
        .bus_id = 1,
        .address = 5,
        // VID/PID/DID unused.
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    {
        .bus_id = 1,
        .address = 6,
        // VID/PID/DID unused.
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
};

static const pbus_metadata_t i2c_metadata[] = {{
    .type = DEVICE_METADATA_I2C_CHANNELS,
    .data_buffer = reinterpret_cast<const uint8_t*>(&i2c_channels),
    .data_size = sizeof(i2c_channels),
}};

}  // namespace

zx_status_t TestBoard::I2cInit() {
  pbus_dev_t i2c_dev = {};
  i2c_dev.name = "i2c";
  i2c_dev.vid = PDEV_VID_TEST;
  i2c_dev.pid = PDEV_PID_PBUS_TEST;
  i2c_dev.did = PDEV_DID_TEST_I2C;
  i2c_dev.metadata_list = i2c_metadata;
  i2c_dev.metadata_count = countof(i2c_metadata);

  zx_status_t status = pbus_.DeviceAdd(&i2c_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_test
