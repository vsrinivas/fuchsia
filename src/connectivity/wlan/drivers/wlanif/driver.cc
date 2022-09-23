// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <zircon/status.h>

#include <wlan/drivers/log_instance.h>

#include "debug.h"
#include "device.h"
#include "src/connectivity/wlan/drivers/wlanif/wlanif-bind.h"

zx_status_t wlan_fullmac_bind(void* ctx, zx_device_t* device) {
  wlan::drivers::log::Instance::Init(kFiltSetting);
  ltrace_fn();

  wlan_fullmac_impl_protocol_t wlan_fullmac_impl_proto;
  zx_status_t status;
  status = device_get_protocol(device, ZX_PROTOCOL_WLAN_FULLMAC_IMPL,
                               static_cast<void*>(&wlan_fullmac_impl_proto));
  if (status != ZX_OK) {
    lerror("bind: no wlan_fullmac_impl protocol (%s)", zx_status_get_string(status));
    return ZX_ERR_INTERNAL;
  }

  auto wlan_fullmac_dev = std::make_unique<wlanif::Device>(device, wlan_fullmac_impl_proto);

  status = wlan_fullmac_dev->Bind();
  if (status != ZX_OK) {
    lerror("could not bind: %s", zx_status_get_string(status));
  } else {
    // devhost is now responsible for the memory used by wlandev. It will be
    // cleaned up in the Device::Release() method.
    wlan_fullmac_dev.release();
  }
  return status;
}

static constexpr zx_driver_ops_t wlan_fullmac_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = wlan_fullmac_bind;
  return ops;
}();

ZIRCON_DRIVER(wlan, wlan_fullmac_driver_ops, "zircon", "0.1");
