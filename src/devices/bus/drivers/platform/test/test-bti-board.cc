// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/syscalls.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/driver.h>
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
  ddk::PBusProtocolClient pbus(parent);
  if (!pbus.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto board = std::make_unique<TestBoard>(parent);

  zx_status_t status = board->DdkAdd("test-board", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "TestBoard::Create: DdkAdd failed: %d", status);
    return status;
  }
  __UNUSED auto dummy = board.release();

  static const pbus_bti_t kBtis[] = {
      {
          .iommu_index = 0,
          .bti_id = 0,
      },
  };

  static const pbus_dev_t kBtiDevice = []() {
    pbus_dev_t dev = {};
    dev.name = "bti-test";
    dev.vid = PDEV_VID_TEST;
    dev.pid = PDEV_PID_PBUS_TEST;
    dev.did = PDEV_DID_TEST_BTI;
    dev.bti_list = kBtis;
    dev.bti_count = std::size(kBtis);
    return dev;
  }();

  status = pbus.DeviceAdd(&kBtiDevice);
  if (status != ZX_OK) {
    zxlogf(ERROR, "TestBoard::Create: pbus.DeviceAdd failed: %d", status);
  }

  return status;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = TestBoard::Create;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(test_bti_board, driver_ops, "zircon", "0.1");
