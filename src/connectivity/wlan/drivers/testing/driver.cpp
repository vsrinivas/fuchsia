// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/test.h>
#include <lib/async-loop/cpp/loop.h>

#include <memory>

#include "phy-device.h"

// Not guarded by a mutex, because it will be valid between .init and .release
// and nothing else will exist outside those two calls.
static async::Loop* loop = nullptr;

zx_status_t wlanphy_test_init(void** out_ctx) {
  zxlogf(INFO, "%s\n", __func__);
  loop = new async::Loop(&kAsyncLoopConfigNoAttachToThread);
  zx_status_t status = loop->StartThread("wlan-test-loop");
  if (status != ZX_OK) {
    zxlogf(ERROR, "wlanphy_test: could not create event loop: %d\n", status);
    delete loop;
    loop = nullptr;
  }
  zxlogf(INFO, "wlanphy_test: event loop started\n");
  return status;
}

zx_status_t wlanphy_test_bind(void* ctx, zx_device_t* device) {
  zxlogf(INFO, "%s\n", __func__);

  test_protocol_t proto;
  auto status = device_get_protocol(device, ZX_PROTOCOL_TEST,
                                    reinterpret_cast<void*>(&proto));
  if (status != ZX_OK) {
    return status;
  }

  auto dev = std::make_unique<wlan::testing::PhyDevice>(device);
  status = dev->Bind();
  if (status != ZX_OK) {
    zxlogf(ERROR, "wlanphy-test: could not bind: %d\n", status);
  } else {
    // devhost is now responsible for the memory used by wlan-test. It will
    // be cleaned up in the Device::Release() method.
    dev.release();
  }

  return status;
}

void wlanphy_test_release(void* ctx) {
  if (loop != nullptr) {
    loop->Shutdown();
  }
  delete loop;
  loop = nullptr;
}

async_dispatcher_t* wlanphy_async_t() { return loop->dispatcher(); }

static zx_driver_ops_t wlanphy_test_driver_ops = []() {
  zx_driver_ops_t ops;
  ops.version = DRIVER_OPS_VERSION;
  ops.init = wlanphy_test_init;
  ops.bind = wlanphy_test_bind;
  ops.release = wlanphy_test_release;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(wlanphy_test, wlanphy_test_driver_ops, "fuchsia", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST),
ZIRCON_DRIVER_END(wlanphy_test)
