// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/test/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/status.h>

#include <cstdio>
#include <future>
#include <memory>
#include <thread>

#include "fake-device.h"
#include "src/connectivity/telephony/tests/fake-drivers/at-fake-transport/at_fake_bind.h"

zx_status_t at_fake_bind(void* ctx, zx_device_t* device) {
  auto dev = std::make_unique<at_fake::AtDevice>(device);
  auto status = dev->Bind();
  std::printf("%s\n", __func__);
  if (status != ZX_OK) {
    std::printf("at_fake_bind: could not bind: %d\n", status);
  } else {
    dev.release();
  }

  return status;
}

static constexpr zx_driver_ops_t at_fake_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = at_fake_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER(at_fake, at_fake_driver_ops, "zircon", "0.1");
