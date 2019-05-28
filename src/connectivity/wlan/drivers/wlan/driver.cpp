// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <cstdio>
#include <memory>

#include "device.h"

zx_status_t wlan_bind(void* ctx, zx_device_t* device) {
  std::printf("%s\n", __func__);

  wlanmac_protocol_t wlanmac_proto;
  if (device_get_protocol(device, ZX_PROTOCOL_WLANMAC,
                          reinterpret_cast<void*>(&wlanmac_proto))) {
    std::printf("wlan: bind: no wlanmac protocol\n");
    return ZX_ERR_INTERNAL;
  }

  auto wlandev = std::make_unique<wlan::Device>(device, wlanmac_proto);
  auto status = wlandev->Bind();
  if (status != ZX_OK) {
    std::printf("wlan: could not bind: %d\n", status);
  } else {
    // devhost is now responsible for the memory used by wlandev. It will be
    // cleaned up in the Device::Release() method.
    wlandev.release();
  }
  return status;
}

static constexpr zx_driver_ops_t wlan_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = wlan_bind;
    return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(wlan, wlan_driver_ops, "zircon", "0.1", 1)
  BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_WLANMAC),
ZIRCON_DRIVER_END(wlan)
