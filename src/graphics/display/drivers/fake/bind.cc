// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <fbl/alloc_checker.h>

#include "fake-display.h"
#include "src/graphics/display/drivers/fake/fake-display-bind.h"

// main bind function called from dev manager
static zx_status_t fake_display_bind(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<fake_display::FakeDisplay>(&ac, parent);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto status = dev->Bind(/*start_vsync=*/true);
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

static constexpr zx_driver_ops_t fake_display_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = fake_display_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER(fake_display, fake_display_ops, "zircon", "0.1");
