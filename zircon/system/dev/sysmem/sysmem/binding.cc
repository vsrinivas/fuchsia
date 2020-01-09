// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <string.h>

#include <limits>
#include <memory>
#include <utility>

#include <ddk/binding.h>
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

namespace sysmem_driver {
zx_status_t sysmem_init(void** out_driver_ctx) {
  DRIVER_TRACE("sysmem_init() - async_get_default_dispatcher(): %p",
               async_get_default_dispatcher());

  auto driver = std::make_unique<Driver>();
  driver->dispatcher = async_get_default_dispatcher();
  driver->dispatcher_thrd = thrd_current();

  // For now at least, sysmem doesn't unload, so just release() the pointer
  // into *out_driver_ctx to remain allocated for life of this devhost
  // process.
  *out_driver_ctx = reinterpret_cast<void*>(driver.release());
  return ZX_OK;
}

zx_status_t sysmem_bind(void* driver_ctx, zx_device_t* parent_device) {
  DRIVER_TRACE("sysmem_bind()");
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

// clang-format off
ZIRCON_DRIVER_BEGIN(sysmem, sysmem_driver::sysmem_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_SYSMEM),
ZIRCON_DRIVER_END(sysmem)
    // clang-format on
