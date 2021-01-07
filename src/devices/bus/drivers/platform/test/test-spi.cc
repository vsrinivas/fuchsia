// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/spi.h>
#include <ddk/platform-defs.h>

#include "test.h"

namespace board_test {

namespace {

static const spi_channel_t spi_channels[] = {{.bus_id = 0,
                                              .cs = 0,
                                              // VID/PID/DID unused.
                                              .vid = 0,
                                              .pid = 0,
                                              .did = 0}};

static const pbus_metadata_t spi_metadata[] = {{
    .type = DEVICE_METADATA_SPI_CHANNELS,
    .data_buffer = &spi_channels,
    .data_size = sizeof(spi_channels),
}};

}  // namespace

zx_status_t TestBoard::SpiInit() {
  pbus_dev_t spi_dev = {};
  spi_dev.name = "spi";
  spi_dev.vid = PDEV_VID_TEST;
  spi_dev.pid = PDEV_PID_PBUS_TEST;
  spi_dev.did = PDEV_DID_TEST_SPI;
  spi_dev.metadata_list = spi_metadata;
  spi_dev.metadata_count = countof(spi_metadata);

  zx_status_t status = pbus_.DeviceAdd(&spi_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_test
