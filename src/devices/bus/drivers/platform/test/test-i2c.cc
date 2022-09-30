// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include "src/devices/lib/fidl-metadata/i2c.h"
#include "test.h"

namespace board_test {
namespace fpbus = fuchsia_hardware_platform_bus;
using i2c_channel_t = fidl_metadata::i2c::Channel;

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

}  // namespace

zx_status_t TestBoard::I2cInit() {
  fuchsia_hardware_platform_bus::Node i2c_dev = {};
  i2c_dev.name() = "i2c";
  i2c_dev.vid() = PDEV_VID_TEST;
  i2c_dev.pid() = PDEV_PID_PBUS_TEST;
  i2c_dev.did() = PDEV_DID_TEST_I2C;

  auto i2c_status = fidl_metadata::i2c::I2CChannelsToFidl(i2c_channels);
  if (i2c_status.is_error()) {
    zxlogf(ERROR, "%s: failed to fidl encode i2c channels: %d", __func__, i2c_status.error_value());
    return i2c_status.error_value();
  }

  auto& data = i2c_status.value();

  fuchsia_hardware_platform_bus::Metadata i2c_metadata;
  i2c_metadata.type() = DEVICE_METADATA_I2C_CHANNELS;
  i2c_metadata.data() = std::move(data);
  i2c_dev.metadata() =
      std::vector<fuchsia_hardware_platform_bus::Metadata>{std::move(i2c_metadata)};

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('TI2C');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, i2c_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd request failed: %s", __func__, result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd failed: %s", __func__, zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace board_test
