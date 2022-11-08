// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include <cstdio>
#include <memory>

#include "device.h"
#include "src/connectivity/wlan/drivers/wlansoftmac/wlansoftmac_bind.h"

zx_status_t wlan_bind(void* ctx, zx_device_t* device) {
  std::printf("%s\n", __func__);

  auto endpoints = fdf::CreateEndpoints<fuchsia_wlan_softmac::WlanSoftmac>();
  if (endpoints.is_error()) {
    errorf("Failed to create endpoint: %s", zx_status_get_string(endpoints.status_value()));
    return endpoints.status_value();
  }

  auto wlandev = std::make_unique<wlan::Device>(device, std::move(endpoints->client));
  auto status = wlandev->Bind(endpoints->server.TakeHandle());
  if (status != ZX_OK) {
    errorf("Failed to bind: %d\n", status);
    return status;
  }
  // devhost is now responsible for the memory used by wlandev. It will be
  // cleaned up in the Device::EthRelease() method.
  wlandev.release();
  return ZX_OK;
}

static constexpr zx_driver_ops_t wlan_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = wlan_bind;
  return ops;
}();

ZIRCON_DRIVER(wlan, wlan_driver_ops, "zircon", "0.1");
