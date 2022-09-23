// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <zircon/status.h>

#include <mutex>

#include <wlan/drivers/log_instance.h>

#include "debug.h"
#include "device_dfv2.h"
#include "driver.h"
#include "src/connectivity/wlan/drivers/wlanphy/wlanphy-bind.h"

// Not guarded by a mutex, because it will be valid between .init and .release and nothing else will
// exist outside those two calls.
static async::Loop* loop = nullptr;
static std::once_flag flag;

zx_status_t wlanphy_init_loop() {
  zx_status_t status = ZX_OK;
  loop = new async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  status = loop->StartThread("wlanphy-loop");
  if (status != ZX_OK) {
    lerror("could not create event loop: %s", zx_status_get_string(status));
    delete loop;
    loop = nullptr;
    return status;
  } else {
    linfo("event loop started");
  }

  return status;
}

zx_status_t wlanphy_init(void** out_ctx) {
  static zx_status_t status = ZX_ERR_BAD_STATE;
  std::call_once(flag, []() { status = wlanphy_init_loop(); });
  return status;
}

zx_status_t wlanphy_bind(void* ctx, zx_device_t* device) {
  wlan::drivers::log::Instance::Init(kFiltSetting);
  ltrace_fn();
  zx_status_t status;

  auto endpoints = fdf::CreateEndpoints<fuchsia_wlan_wlanphyimpl::WlanphyImpl>();
  if (endpoints.is_error()) {
    lerror("Creating end point error: %s", zx_status_get_string(endpoints.status_value()));
    return endpoints.status_value();
  }

  auto wlanphy_dev = std::make_unique<wlanphy::Device>(device, std::move(endpoints->client));
  if ((status = wlanphy_dev->DeviceAdd()) != ZX_OK) {
    lerror("failed adding wlanphy device: %s", zx_status_get_string(status));
  }

  if ((status = wlanphy_dev->ConnectToWlanphyImpl(endpoints->server.TakeHandle()))) {
    lerror("failed connecting to wlanphyimpl device: %s", zx_status_get_string(status));
  }

  if (status != ZX_OK) {
    lerror("could not bind: %s", zx_status_get_string(status));
  } else {
    // devhost is now responsible for the memory used by wlandev. It will be
    // cleaned up in the Device::Release() method.
    wlanphy_dev.release();
  }
  return status;
}

async_dispatcher_t* wlanphy_async_t() {
  if (loop == nullptr) {
    lerror("Loop is not initialized.");
    return nullptr;
  }

  return loop->dispatcher();
}

void wlanphy_destroy_loop() {
  loop->Shutdown();
  delete loop;
}

static constexpr zx_driver_ops_t wlanphy_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.init = wlanphy_init;
  ops.bind = wlanphy_bind;
  return ops;
}();

// clang-format: off
ZIRCON_DRIVER(wlan, wlanphy_driver_ops, "zircon", "0.1");
