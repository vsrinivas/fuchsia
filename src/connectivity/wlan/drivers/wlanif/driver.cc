// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver.h"

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <zircon/status.h>

#include "device.h"
#include "src/connectivity/wlan/drivers/wlanif/wlanif-bind.h"

zx_status_t wlanif_bind(void* ctx, zx_device_t* device) {
  zxlogf(INFO, "%s", __func__);

  wlanif_impl_protocol_t wlanif_impl_proto;
  zx_status_t status;
  status =
      device_get_protocol(device, ZX_PROTOCOL_WLANIF_IMPL, static_cast<void*>(&wlanif_impl_proto));
  if (status != ZX_OK) {
    zxlogf(ERROR, "wlanif: bind: no wlanif_impl protocol (%s)", zx_status_get_string(status));
    return ZX_ERR_INTERNAL;
  }

  auto wlanif_dev = std::make_unique<wlanif::Device>(device, wlanif_impl_proto);

  status = wlanif_dev->Bind();
  if (status != ZX_OK) {
    zxlogf(ERROR, "wlanif: could not bind: %s", zx_status_get_string(status));
  } else {
    // devhost is now responsible for the memory used by wlandev. It will be
    // cleaned up in the Device::Release() method.
    wlanif_dev.release();
  }
  return status;
}

static constexpr zx_driver_ops_t wlanif_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = wlanif_bind;
  return ops;
}();

ZIRCON_DRIVER(wlan, wlanif_driver_ops, "zircon", "0.1");
