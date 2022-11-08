// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/test/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/status.h>

#include <cstdio>
#include <future>
#include <memory>
#include <thread>

#include "src/connectivity/bluetooth/hci/virtual/bt-hci-virtual-bind.h"
#include "src/connectivity/bluetooth/hci/virtual/controller.h"
#include "src/connectivity/bluetooth/hci/virtual/log.h"

namespace {

zx_status_t DriverBind(void* ctx, zx_device_t* device) {
  logf(TRACE, "DriverBind\n");

  auto dev = std::make_unique<bt_hci_virtual::VirtualController>(device);
  zx_status_t status = dev->Bind();
  if (status != ZX_OK) {
    logf(ERROR, "failed to bind: %s\n", zx_status_get_string(status));
  } else {
    dev.release();
  }

  return status;
}

static constexpr zx_driver_ops_t bt_hci_virtual_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = DriverBind,
};

}  // namespace

ZIRCON_DRIVER(bt_hci_virtual, bt_hci_virtual_driver_ops, "zircon", "0.1");
