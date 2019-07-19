// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>

namespace gpio {

extern zx_status_t mt8167_gpio_bind(void* ctx, zx_device_t* parent);

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = mt8167_gpio_bind;
  return ops;
}();

}  // namespace gpio

ZIRCON_DRIVER_BEGIN(mt8167_gpio, gpio::driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_GPIO), ZIRCON_DRIVER_END(mt8167_gpio)
