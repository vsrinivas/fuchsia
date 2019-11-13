// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/status.h>

#include <cstdio>
#include <future>
#include <memory>
#include <thread>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/test.h>

#include "fake-device.h"

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
ZIRCON_DRIVER_BEGIN(at_fake, at_fake_driver_ops, "zircon", "0.1", 3)
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_TEL_TEST),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_AT_MODEM),
ZIRCON_DRIVER_END(at_fake)
