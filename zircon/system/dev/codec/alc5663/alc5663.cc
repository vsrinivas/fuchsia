// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "alc5663.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <endian.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <lib/device-protocol/i2c.h>
#include <sys/types.h>
#include <zircon/assert.h>
#include <zircon/status.h>

namespace audio::alc5663 {

zx_status_t Alc5663Device::CreateAndBind(zx_device_t* parent, Alc5663Device** result) {
  // Create the codec device.
  fbl::AllocChecker ac;
  auto device = fbl::unique_ptr<Alc5663Device>(new (&ac) Alc5663Device(parent));
  if (!ac.check()) {
    zxlogf(ERROR, "alc5663: out of memory attempting to allocate device\n");
    return ZX_ERR_NO_MEMORY;
  }

  // Get access to the I2C protocol.
  zx_status_t status = device_get_protocol(device->parent(), ZX_PROTOCOL_I2C, &device->i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "alc5663: could not get I2C protocol: %s\n", zx_status_get_string(status));
    return status;
  }

  // Add the device.
  status = device->DdkAdd("alc5663");
  if (status != ZX_OK) {
    zxlogf(ERROR, "alc5663: could not add device: %s\n", zx_status_get_string(status));
    return status;
  }

  // Provide a reference to the caller if requested.
  if (result != nullptr) {
    *result = device.get();
  }

  (void)device.release();  // `dev` will be deleted when DdkRelease() is called.
  return ZX_OK;
}

void Alc5663Device::DdkUnbind() {}

void Alc5663Device::DdkRelease() { delete this; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = [](void* /*ctx*/, zx_device_t* parent) {
    return Alc5663Device::CreateAndBind(parent);
  };
  return ops;
}();

}  // namespace audio::alc5663

// clang-format off
ZIRCON_DRIVER_BEGIN(alc5663, audio::alc5663::driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_ACPI_HID_0_3, 0x31304543), // '10EC' (Realtek)
    BI_MATCH_IF(EQ, BIND_ACPI_HID_4_7, 0x35363633), // '5663'
ZIRCON_DRIVER_END(alc5663)
    // clang-format on
