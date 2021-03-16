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
#include "src/connectivity/telephony/drivers/qmi-fake-transport/qmi_fake_bind.h"

zx_status_t qmi_fake_bind(void* ctx, zx_device_t* device) {
  auto dev = std::make_unique<qmi_fake::QmiDevice>(device);
  auto status = dev->Bind();
  std::printf("%s\n", __func__);
  if (status != ZX_OK) {
    std::printf("qmi_fake_bind: could not bind: %d\n", status);
  } else {
    dev.release();
  }

  return status;
}

static constexpr zx_driver_ops_t qmi_fake_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = qmi_fake_bind;
  return ops;
}();

ZIRCON_DRIVER(qmi_fake, qmi_fake_driver_ops, "zircon", "0.1");
