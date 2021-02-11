// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <string.h>

#include <limits>
#include <memory>
#include <utility>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#include "device.h"
#include "driver.h"
#include "macros.h"
#include "src/devices/sysmem/drivers/sysmem/sysmem-bind.h"

namespace sysmem_driver {
zx_status_t sysmem_init(void** out_driver_ctx) {
  auto driver = std::make_unique<Driver>();

  // For now at least, sysmem doesn't unload, so just release() the pointer
  // into *out_driver_ctx to remain allocated for life of this devhost
  // process.
  *out_driver_ctx = reinterpret_cast<void*>(driver.release());
  return ZX_OK;
}

zx_status_t sysmem_bind(void* driver_ctx, zx_device_t* parent_device) {
  DRIVER_DEBUG("sysmem_bind()");
  Driver* driver = reinterpret_cast<Driver*>(driver_ctx);

  auto device = std::make_unique<Device>(parent_device, driver);

  auto status = device->Bind();
  if (status != ZX_OK) {
    DRIVER_ERROR("Bind() failed - status: %d\n", status);
    return status;
  }
  ZX_DEBUG_ASSERT(status == ZX_OK);

  // For now at least, there's only one sysmem device and it isn't ever
  // removed, so just release() the pointer so it lives as long as this
  // devhost process.
  __UNUSED auto ptr = device.release();

  return ZX_OK;
}

// -Werror=missing-field-initializers is more paranoid than I want here.
zx_driver_ops_t sysmem_driver_ops = [] {
  zx_driver_ops_t tmp{};
  tmp.version = DRIVER_OPS_VERSION;
  tmp.init = sysmem_init;
  tmp.bind = sysmem_bind;
  return tmp;
}();

}  // namespace sysmem_driver

ZIRCON_DRIVER(sysmem, sysmem_driver::sysmem_driver_ops, "zircon", "0.1");
