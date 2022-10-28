// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpio-init.h"

#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <lib/ddk/metadata.h>

#include <ddk/metadata/init-step.h>

#include "src/devices/gpio/drivers/gpio-init/gpio-init-bind.h"

namespace gpio_init {

static_assert(fuchsia_hardware_gpio_init::wire::kMaxGpioFragmentName == ZX_DEVICE_NAME_MAX);

zx_status_t GpioInit::Create(void* ctx, zx_device_t* parent) {
  auto decoded = ddk::GetEncodedMetadata<fuchsia_hardware_gpio_init::wire::GpioInitMetadata>(
      parent, DEVICE_METADATA_GPIO_INIT_STEPS);
  if (!decoded.is_ok()) {
    zxlogf(ERROR, "Failed to decode metadata: %s", decoded.status_string());
    return decoded.status_value();
  }

  auto device = std::make_unique<GpioInit>(parent);
  device->ConfigureGpios(*decoded->PrimaryObject());

  zx_device_prop_t props[] = {
      {BIND_INIT_STEP, 0, BIND_INIT_STEP_GPIO},
  };

  zx_status_t status = device->DdkAdd(
      ddk::DeviceAddArgs("gpio-init").set_flags(DEVICE_ADD_ALLOW_MULTI_COMPOSITE).set_props(props));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to add gpio-init: %s", zx_status_get_string(status));
    return status;
  }

  __UNUSED auto _ = device.release();
  return ZX_OK;
}

void GpioInit::ConfigureGpios(const fuchsia_hardware_gpio_init::wire::GpioInitMetadata& metadata) {
  for (const auto& step : metadata.steps) {
    char fragment_name[ZX_DEVICE_NAME_MAX + 1] = {};
    memcpy(fragment_name, step.fragment_name.data(), step.fragment_name.size());

    ddk::GpioProtocolClient gpio(parent(), fragment_name);
    if (!gpio.is_valid()) {
      zxlogf(ERROR, "Failed to get GPIO protocol for fragment %s", fragment_name);
      continue;
    }

    zx_status_t status;

    if (step.options.has_alt_function()) {
      if ((status = gpio.SetAltFunction(step.options.alt_function())) != ZX_OK) {
        zxlogf(ERROR, "SetAltFunction(%lu) failed for %s: %s", step.options.drive_strength_ua(),
               fragment_name, zx_status_get_string(status));
      }
    }

    if (step.options.has_input_flags()) {
      if ((status = gpio.ConfigIn(static_cast<uint32_t>(step.options.input_flags()))) != ZX_OK) {
        zxlogf(ERROR, "ConfigIn(%u) failed for %s: %s",
               static_cast<uint32_t>(step.options.input_flags()), fragment_name,
               zx_status_get_string(status));
      }
    }

    if (step.options.has_output_value()) {
      if ((status = gpio.ConfigOut(step.options.output_value())) != ZX_OK) {
        zxlogf(ERROR, "ConfigOut(%u) failed for %s: %s", step.options.output_value(), fragment_name,
               zx_status_get_string(status));
      }
    }

    if (step.options.has_drive_strength_ua()) {
      uint64_t actual_ds;
      if ((status = gpio.SetDriveStrength(step.options.drive_strength_ua(), &actual_ds)) != ZX_OK) {
        zxlogf(ERROR, "SetDriveStrength(%lu) failed for %s: %s", step.options.drive_strength_ua(),
               fragment_name, zx_status_get_string(status));
      } else if (actual_ds != step.options.drive_strength_ua()) {
        zxlogf(WARNING, "Actual drive strength (%lu) doesn't match expected (%lu) for %s",
               actual_ds, step.options.drive_strength_ua(), fragment_name);
      }
    }
  }
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = GpioInit::Create;
  return ops;
}();

}  // namespace gpio_init

ZIRCON_DRIVER(gpio_init, gpio_init::driver_ops, "zircon", "0.1");
