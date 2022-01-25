// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpio-light.h"

#include <assert.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <memory>

#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>

#include "src/ui/light/drivers/gpio-light/gpio-light-bind.h"

namespace gpio_light {

void GpioLight::GetNumLights(GetNumLightsRequestView request,
                             GetNumLightsCompleter::Sync& completer) {
  completer.Reply(gpio_count_);
}

void GpioLight::GetNumLightGroups(GetNumLightGroupsRequestView request,
                                  GetNumLightGroupsCompleter::Sync& completer) {
  completer.Reply(0);
}

void GpioLight::GetInfo(GetInfoRequestView request, GetInfoCompleter::Sync& completer) {
  if (request->index >= gpio_count_) {
    completer.ReplyError(fuchsia_hardware_light::wire::LightError::kInvalidIndex);
    return;
  }

  char name[kNameLength + 2];
  if (names_.size() > request->index) {
    snprintf(name, sizeof(kNameLength), "%s\n", names_[request->index].name);
  } else {
    // Return "gpio-X" if no metadata was provided.
    snprintf(name, sizeof(name), "gpio-%u\n", request->index);
  }

  completer.ReplySuccess({
      .name = ::fidl::StringView(name, strlen(name)),
      .capability = fuchsia_hardware_light::wire::Capability::kSimple,
  });
}

void GpioLight::GetCurrentSimpleValue(GetCurrentSimpleValueRequestView request,
                                      GetCurrentSimpleValueCompleter::Sync& completer) {
  if (request->index >= gpio_count_) {
    completer.ReplyError(fuchsia_hardware_light::wire::LightError::kInvalidIndex);
    return;
  }

  uint8_t value;
  if (gpios_[request->index].Read(&value) != ZX_OK) {
    completer.ReplyError(fuchsia_hardware_light::wire::LightError::kFailed);
  } else {
    completer.ReplySuccess(value);
  }
}

void GpioLight::SetSimpleValue(SetSimpleValueRequestView request,
                               SetSimpleValueCompleter::Sync& completer) {
  if (request->index >= gpio_count_) {
    completer.ReplyError(fuchsia_hardware_light::wire::LightError::kInvalidIndex);
    return;
  }

  if (gpios_[request->index].Write(request->value) != ZX_OK) {
    completer.ReplyError(fuchsia_hardware_light::wire::LightError::kFailed);
  } else {
    completer.ReplySuccess();
  }
}

void GpioLight::GetCurrentBrightnessValue(GetCurrentBrightnessValueRequestView request,
                                          GetCurrentBrightnessValueCompleter::Sync& completer) {
  completer.ReplyError(fuchsia_hardware_light::wire::LightError::kNotSupported);
}

void GpioLight::SetBrightnessValue(SetBrightnessValueRequestView request,
                                   SetBrightnessValueCompleter::Sync& completer) {
  completer.ReplyError(fuchsia_hardware_light::wire::LightError::kNotSupported);
}

void GpioLight::GetCurrentRgbValue(GetCurrentRgbValueRequestView request,
                                   GetCurrentRgbValueCompleter::Sync& completer) {
  completer.ReplyError(fuchsia_hardware_light::wire::LightError::kNotSupported);
}

void GpioLight::SetRgbValue(SetRgbValueRequestView request, SetRgbValueCompleter::Sync& completer) {
  completer.ReplyError(fuchsia_hardware_light::wire::LightError::kNotSupported);
}

void GpioLight::DdkRelease() { delete this; }

zx_status_t GpioLight::Create(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<GpioLight>(new (&ac) GpioLight(parent));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto status = dev->Init();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();
  return ZX_OK;
}

zx_status_t GpioLight::Init() {
  auto fragment_count = DdkGetFragmentCount();
  if (fragment_count <= 0) {
    return ZX_ERR_INTERNAL;
  }
  // fragment 0 is platform device, only used for passing metadata.
  gpio_count_ = fragment_count - 1;
  auto names = ddk::GetMetadataArray<name_t>(parent(), DEVICE_METADATA_NAME);
  if (!names.is_ok()) {
    return names.error_value();
  }
  if (gpio_count_ != names->size()) {
    zxlogf(ERROR, "%s: expected %u gpio names, got %zu", __func__, gpio_count_, names->size());
    return ZX_ERR_INTERNAL;
  }

  names_ = names.value();

  composite_device_fragment_t fragments[fragment_count];
  size_t actual;
  DdkGetFragments(fragments, fragment_count, &actual);
  if (actual != fragment_count) {
    return ZX_ERR_INTERNAL;
  }

  fbl::AllocChecker ac;
  auto* gpios = new (&ac) ddk::GpioProtocolClient[gpio_count_];
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  gpios_.reset(gpios, gpio_count_);

  for (uint32_t i = 0; i < gpio_count_; i++) {
    auto* gpio = &gpios_[i];
    auto status = device_get_protocol(fragments[i + 1].device, ZX_PROTOCOL_GPIO, gpio);
    if (status != ZX_OK) {
      return status;
    }
    status = gpio->ConfigOut(0);
    if (status != ZX_OK) {
      zxlogf(ERROR, "gpio-light: ConfigOut failed for gpio %u", i);
      return status;
    }
  }

  return DdkAdd("gpio-light", DEVICE_ADD_NON_BINDABLE);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = GpioLight::Create;
  return ops;
}();

}  // namespace gpio_light

ZIRCON_DRIVER(gpio_light, gpio_light::driver_ops, "zircon", "0.1");
