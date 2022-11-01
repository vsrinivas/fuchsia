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
#include <ddk/metadata/init-step.h>
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

  // Process init metadata while we are still the exclusive owner of the GPIO client.
  GpioInitDevice::Create(parent, &gpio);

  auto pins = ddk::GetMetadataArray<gpio_pin_t>(parent, DEVICE_METADATA_GPIO_PINS);
  if (!pins.is_ok()) {
    return pins.error_value();
  }

  // Make sure that the list of GPIO pins has no duplicates.
  auto gpio_cmp_lt = [](gpio_pin_t& lhs, gpio_pin_t& rhs) { return lhs.pin < rhs.pin; };
  auto gpio_cmp_eq = [](gpio_pin_t& lhs, gpio_pin_t& rhs) { return lhs.pin == rhs.pin; };
  std::sort(pins.value().begin(), pins.value().end(), gpio_cmp_lt);
  auto result = std::adjacent_find(pins.value().begin(), pins.value().end(), gpio_cmp_eq);
  if (result != pins.value().end()) {
    zxlogf(ERROR, "gpio pin '%d' was published more than once", result->pin);
    return ZX_ERR_INVALID_ARGS;
  }

  for (auto pin : pins.value()) {
    fbl::AllocChecker ac;
    std::unique_ptr<GpioDevice> dev(new (&ac) GpioDevice(parent, &gpio, pin.pin, pin.name));
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

void GpioInitDevice::Create(zx_device_t* parent, const ddk::GpioImplProtocolClient& gpio) {
  // Don't add the init device if anything goes wrong here, as the hardware may be in a state that
  // child devices don't expect.
  auto decoded = ddk::GetEncodedMetadata<fuchsia_hardware_gpio_init::wire::GpioInitMetadata>(
      parent, DEVICE_METADATA_GPIO_INIT_STEPS);
  if (!decoded.is_ok()) {
    if (decoded.status_value() == ZX_ERR_NOT_FOUND) {
      zxlogf(INFO, "No init metadata provided");
    } else {
      zxlogf(ERROR, "Failed to decode metadata: %s", decoded.status_string());
    }
    return;
  }

  auto device = std::make_unique<GpioInitDevice>(parent);
  if (device->ConfigureGpios(*decoded->PrimaryObject(), gpio) != ZX_OK) {
    return;
  }

  zx_device_prop_t props[] = {
      {BIND_INIT_STEP, 0, BIND_INIT_STEP_GPIO},
  };

  zx_status_t status = device->DdkAdd(
      ddk::DeviceAddArgs("gpio-init").set_flags(DEVICE_ADD_ALLOW_MULTI_COMPOSITE).set_props(props));
  if (status == ZX_OK) {
    __UNUSED auto _ = device.release();
  } else {
    zxlogf(ERROR, "Failed to add gpio-init: %s", zx_status_get_string(status));
  }
}

zx_status_t GpioInitDevice::ConfigureGpios(
    const fuchsia_hardware_gpio_init::wire::GpioInitMetadata& metadata,
    const ddk::GpioImplProtocolClient& gpio) {
  // Log errors but continue processing to put as many GPIOs as possible into the requested state.
  zx_status_t return_status = ZX_OK;
  for (const auto& step : metadata.steps) {
    zx_status_t status;

    if (step.options.has_alt_function()) {
      if ((status = gpio.SetAltFunction(step.index, step.options.alt_function())) != ZX_OK) {
        zxlogf(ERROR, "SetAltFunction(%lu) failed for %u: %s", step.options.drive_strength_ua(),
               step.index, zx_status_get_string(status));
        return_status = status;
      }
    }

    if (step.options.has_input_flags()) {
      status = gpio.ConfigIn(step.index, static_cast<uint32_t>(step.options.input_flags()));
      if (status != ZX_OK) {
        zxlogf(ERROR, "ConfigIn(%u) failed for %u: %s",
               static_cast<uint32_t>(step.options.input_flags()), step.index,
               zx_status_get_string(status));
        return_status = status;
      }
    }

    if (step.options.has_output_value()) {
      if ((status = gpio.ConfigOut(step.index, step.options.output_value())) != ZX_OK) {
        zxlogf(ERROR, "ConfigOut(%u) failed for %u: %s", step.options.output_value(), step.index,
               zx_status_get_string(status));
        return_status = status;
      }
    }

    if (step.options.has_drive_strength_ua()) {
      uint64_t actual_ds;
      status = gpio.SetDriveStrength(step.index, step.options.drive_strength_ua(), &actual_ds);
      if (status != ZX_OK) {
        zxlogf(ERROR, "SetDriveStrength(%lu) failed for %u: %s", step.options.drive_strength_ua(),
               step.index, zx_status_get_string(status));
        return_status = status;
      } else if (actual_ds != step.options.drive_strength_ua()) {
        zxlogf(WARNING, "Actual drive strength (%lu) doesn't match expected (%lu) for %u",
               actual_ds, step.options.drive_strength_ua(), step.index);
        return_status = ZX_ERR_BAD_STATE;
      }
    }
  }

  return return_status;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = GpioDevice::Create;
  return ops;
}();

}  // namespace gpio

ZIRCON_DRIVER(gpio, gpio::driver_ops, "zircon", "0.1");
