// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include <cstdio>
#include <memory>

#include "device.h"
#include "src/connectivity/wlan/drivers/wlan/wlan_bind.h"

zx_status_t wlan_bind(void* ctx, zx_device_t* device) {
  std::printf("%s\n", __func__);

  wlanmac_protocol_t wlanmac_proto;
  if (device_get_protocol(device, ZX_PROTOCOL_WLANMAC, reinterpret_cast<void*>(&wlanmac_proto))) {
    std::printf("wlan: bind: no wlanmac protocol\n");
    return ZX_ERR_INTERNAL;
  }

  auto wlandev = std::make_unique<wlan::Device>(device, wlanmac_proto);
  auto status = wlandev->Bind();
  if (status != ZX_OK) {
    std::printf("wlan: could not bind: %d\n", status);
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
