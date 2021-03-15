// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/platform-defs.h>

#include <ddk/driver.h>

#include "src/devices/gpio/drivers/qcom-gpio/qcom-gpio-bind.h"

namespace gpio {

extern zx_status_t qcom_gpio_bind(void* ctx, zx_device_t* parent);

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = qcom_gpio_bind;
  return ops;
}();

}  // namespace gpio

ZIRCON_DRIVER(qcom_gpio, gpio::driver_ops, "zircon", "0.1");
