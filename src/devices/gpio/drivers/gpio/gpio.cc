// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpio.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/metadata/gpio.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#include "src/devices/gpio/drivers/gpio/gpio_bind.h"

namespace gpio {

zx_status_t GpioDevice::GpioConfigIn(uint32_t flags) {
  fbl::AutoLock lock(&lock_);
  return gpio_.ConfigIn(pin_, flags);
}

zx_status_t GpioDevice::GpioConfigOut(uint8_t initial_value) {
  fbl::AutoLock lock(&lock_);
  return gpio_.ConfigOut(pin_, initial_value);
}

zx_status_t GpioDevice::GpioSetAltFunction(uint64_t function) {
  fbl::AutoLock lock(&lock_);
  return gpio_.SetAltFunction(pin_, function);
}

zx_status_t GpioDevice::GpioRead(uint8_t* out_value) {
  fbl::AutoLock lock(&lock_);
  return gpio_.Read(pin_, out_value);
}

zx_status_t GpioDevice::GpioWrite(uint8_t value) {
  fbl::AutoLock lock(&lock_);
  return gpio_.Write(pin_, value);
}

zx_status_t GpioDevice::GpioGetInterrupt(uint32_t flags, zx::interrupt* out_irq) {
  fbl::AutoLock lock(&lock_);
  return gpio_.GetInterrupt(pin_, flags, out_irq);
}

zx_status_t GpioDevice::GpioReleaseInterrupt() {
  fbl::AutoLock lock(&lock_);
  return gpio_.ReleaseInterrupt(pin_);
}

zx_status_t GpioDevice::GpioSetPolarity(gpio_polarity_t polarity) {
  fbl::AutoLock lock(&lock_);
  return gpio_.SetPolarity(pin_, polarity);
}

zx_status_t GpioDevice::GpioGetDriveStrength(uint64_t* ds_ua) {
  fbl::AutoLock lock(&lock_);
  return gpio_.GetDriveStrength(pin_, ds_ua);
}

zx_status_t GpioDevice::GpioSetDriveStrength(uint64_t ds_ua, uint64_t* out_actual_ds_ua) {
  fbl::AutoLock lock(&lock_);
  return gpio_.SetDriveStrength(pin_, ds_ua, out_actual_ds_ua);
}

void GpioDevice::DdkRelease() { delete this; }

zx_status_t GpioDevice::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
  fbl::AutoLock lock(&lock_);
  if (opened_) {
    return ZX_ERR_ALREADY_BOUND;
  }
  opened_ = true;
  return ZX_OK;
}

zx_status_t GpioDevice::DdkClose(uint32_t flags) {
  fbl::AutoLock lock(&lock_);
  gpio_.ReleaseInterrupt(pin_);
  opened_ = false;
  return ZX_OK;
}

zx_status_t GpioDevice::Create(void* ctx, zx_device_t* parent) {
  gpio_impl_protocol_t gpio;
  auto status = device_get_protocol(parent, ZX_PROTOCOL_GPIO_IMPL, &gpio);
  if (status != ZX_OK) {
    return status;
  }

  auto pins = ddk::GetMetadataArray<gpio_pin_t>(parent, DEVICE_METADATA_GPIO_PINS);
  if (!pins.is_ok()) {
    return pins.error_value();
  }

  for (auto pin : pins.value()) {
    fbl::AllocChecker ac;
    std::unique_ptr<GpioDevice> dev(new (&ac) GpioDevice(parent, &gpio, pin.pin));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    char name[20];
    snprintf(name, sizeof(name), "gpio-%u", pin.pin);
    zx_device_prop_t props[] = {
        {BIND_GPIO_PIN, 0, pin.pin},
    };

    status = dev->DdkAdd(ddk::DeviceAddArgs(name).set_props(props));
    if (status != ZX_OK) {
      return status;
    }

    // dev is now owned by devmgr.
    __UNUSED auto ptr = dev.release();
  }

  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = GpioDevice::Create;
  return ops;
}();

}  // namespace gpio

ZIRCON_DRIVER(gpio, gpio::driver_ops, "zircon", "0.1");
