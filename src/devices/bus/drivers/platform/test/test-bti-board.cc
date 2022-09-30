// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/syscalls.h>

#include <memory>

#include <ddktl/device.h>

#include "src/devices/bus/drivers/platform/test/test-bti-board-bind.h"

namespace {

class TestBoard : public ddk::Device<TestBoard> {
 public:
  explicit TestBoard(zx_device_t* parent) : ddk::Device<TestBoard>(parent) {}

  static zx_status_t Create(void*, zx_device_t* parent);

  void DdkRelease() { delete this; }
};

zx_status_t TestBoard::Create(void*, zx_device_t* parent) {
  auto endpoints = fdf::CreateEndpoints<fuchsia_hardware_platform_bus::PlatformBus>();
  if (endpoints.is_error()) {
    return endpoints.error_value();
  }

  zx_status_t status = device_connect_runtime_protocol(
      parent, fuchsia_hardware_platform_bus::Service::PlatformBus::ServiceName,
      fuchsia_hardware_platform_bus::Service::PlatformBus::Name,
      endpoints->server.TakeHandle().release());
  if (status != ZX_OK) {
    return status;
  }

  fdf::WireSyncClient<fuchsia_hardware_platform_bus::PlatformBus> pbus(
      std::move(endpoints->client));

  auto board = std::make_unique<TestBoard>(parent);

  status = board->DdkAdd("test-board", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "TestBoard::Create: DdkAdd failed: %d", status);
    return status;
  }
  __UNUSED auto dummy = board.release();

  static const std::vector<fuchsia_hardware_platform_bus::Bti> kBtis{
      []() {
        fuchsia_hardware_platform_bus::Bti ret;
        ret.iommu_index() = 0;
        ret.bti_id() = 0;
        return ret;
      }(),
  };

  static const fuchsia_hardware_platform_bus::Node kBtiDevice = []() {
    fuchsia_hardware_platform_bus::Node dev = {};
    dev.name() = "bti-test";
    dev.vid() = PDEV_VID_TEST;
    dev.pid() = PDEV_PID_PBUS_TEST;
    dev.did() = PDEV_DID_TEST_BTI;
    dev.bti() = std::move(kBtis);
    return dev;
  }();

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('TEST');
  auto result = pbus.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, kBtiDevice));
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

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = TestBoard::Create;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(test_bti_board, driver_ops, "zircon", "0.1");
