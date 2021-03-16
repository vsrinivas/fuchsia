// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/driver.h>

#include "src/devices/nand/drivers/ram-nand/ram-nand-bind.h"
#include "src/devices/nand/drivers/ram-nand/ram-nand-ctl.h"

static constexpr zx_driver_ops_t ram_nand_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = RamNandDriverBind;
  return ops;
}();

ZIRCON_DRIVER(ram_nand, ram_nand_driver_ops, "zircon", "0.1");
