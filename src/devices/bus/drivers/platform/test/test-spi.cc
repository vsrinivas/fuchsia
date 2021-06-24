// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include "src/devices/lib/fidl-metadata/spi.h"
#include "test.h"

namespace board_test {

namespace {
using spi_channel_t = fidl_metadata::spi::Channel;

static const spi_channel_t spi_channels[] = {{.bus_id = 0,
                                              .cs = 0,
                                              // VID/PID/DID unused.
                                              .vid = 0,
                                              .pid = 0,
                                              .did = 0}};

}  // namespace

zx_status_t TestBoard::SpiInit() {
  pbus_dev_t spi_dev = {};
  spi_dev.name = "spi";
  spi_dev.vid = PDEV_VID_TEST;
  spi_dev.pid = PDEV_PID_PBUS_TEST;
  spi_dev.did = PDEV_DID_TEST_SPI;

  auto spi_status =
      fidl_metadata::spi::SpiChannelsToFidl(spi_channels);
  if (spi_status.is_error()) {
    zxlogf(ERROR, "%s: failed to encode spi channels to fidl: %d", __func__, spi_status.error_value());
    return spi_status.error_value();
  }
  auto& data = spi_status.value();


  pbus_metadata_t metadata{
      .type = DEVICE_METADATA_SPI_CHANNELS,
      .data_buffer = data.data(),
      .data_size = data.size(),
  };

  spi_dev.metadata_list = &metadata;
  spi_dev.metadata_count = 1;

  zx_status_t status = pbus_.DeviceAdd(&spi_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_test
