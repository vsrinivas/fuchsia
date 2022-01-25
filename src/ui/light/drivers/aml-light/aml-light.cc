// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-light.h"

#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <string.h>

#include <cmath>

#include <ddk/metadata/lights.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>

#include "src/ui/light/drivers/aml-light/aml_light_bind.h"

namespace aml_light {

namespace {

constexpr double kMaxBrightness = 1.0;
constexpr double kMinBrightness = 0.0;
constexpr uint32_t kPwmPeriodNs = 170625;

}  // namespace

zx_status_t LightDevice::Init(bool init_on) {
  zx_status_t status = ZX_OK;
  if (pwm_.has_value() && ((status = pwm_->Enable()) != ZX_OK)) {
    return status;
  }
  return pwm_.has_value() ? SetBrightnessValue(init_on ? kMaxBrightness : kMinBrightness)
                          : SetSimpleValue(init_on);
}

zx_status_t LightDevice::SetSimpleValue(bool value) {
  if (pwm_.has_value()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = ZX_OK;
  if ((status = gpio_.Write(value)) != ZX_OK) {
    zxlogf(ERROR, "%s: GPIO write failed", __func__);
    return status;
  }

  value_ = value;
  return ZX_OK;
}

zx_status_t LightDevice::SetBrightnessValue(double value) {
  if (!pwm_.has_value()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if ((value > kMaxBrightness) || (value < kMinBrightness) || std::isnan(value)) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = ZX_OK;
  aml_pwm::mode_config regular = {aml_pwm::ON, {}};
  pwm_config_t config = {
      .polarity = false,
      .period_ns = kPwmPeriodNs,
      .duty_cycle = static_cast<float>(value * 100.0 / (kMaxBrightness * 1.0)),
      .mode_config_buffer = reinterpret_cast<uint8_t*>(&regular),
      .mode_config_size = sizeof(regular),
  };
  if ((status = pwm_->SetConfig(&config)) != ZX_OK) {
    zxlogf(ERROR, "%s: PWM set config failed", __func__);
    return status;
  }

  value_ = value;
  return ZX_OK;
}

void AmlLight::GetNumLights(GetNumLightsRequestView request,
                            GetNumLightsCompleter::Sync& completer) {
  completer.Reply(static_cast<uint32_t>(lights_.size()));
}

void AmlLight::GetNumLightGroups(GetNumLightGroupsRequestView request,
                                 GetNumLightGroupsCompleter::Sync& completer) {
  completer.Reply(0);
}

void AmlLight::GetInfo(GetInfoRequestView request, GetInfoCompleter::Sync& completer) {
  if (request->index >= lights_.size()) {
    completer.ReplyError(LightError::kInvalidIndex);
    return;
  }
  auto name = lights_[request->index].GetName();
  completer.ReplySuccess({
      .name = ::fidl::StringView::FromExternal(name),
      .capability = lights_[request->index].GetCapability(),
  });
}

void AmlLight::GetCurrentSimpleValue(GetCurrentSimpleValueRequestView request,
                                     GetCurrentSimpleValueCompleter::Sync& completer) {
  if (request->index >= lights_.size()) {
    completer.ReplyError(LightError::kInvalidIndex);
    return;
  }
  if (lights_[request->index].GetCapability() == Capability::kSimple) {
    completer.ReplySuccess(lights_[request->index].GetCurrentSimpleValue());
  } else {
    completer.ReplyError(LightError::kNotSupported);
  }
}

void AmlLight::SetSimpleValue(SetSimpleValueRequestView request,
                              SetSimpleValueCompleter::Sync& completer) {
  if (request->index >= lights_.size()) {
    completer.ReplyError(LightError::kInvalidIndex);
    return;
  }
  if (lights_[request->index].SetSimpleValue(request->value) != ZX_OK) {
    completer.ReplyError(LightError::kFailed);
  } else {
    completer.ReplySuccess();
  }
}

void AmlLight::GetCurrentBrightnessValue(GetCurrentBrightnessValueRequestView request,
                                         GetCurrentBrightnessValueCompleter::Sync& completer) {
  if (request->index >= lights_.size()) {
    completer.ReplyError(LightError::kInvalidIndex);
    return;
  }
  if (lights_[request->index].GetCapability() == Capability::kBrightness) {
    completer.ReplySuccess(lights_[request->index].GetCurrentBrightnessValue());
  } else {
    completer.ReplyError(LightError::kNotSupported);
  }
}

void AmlLight::SetBrightnessValue(SetBrightnessValueRequestView request,
                                  SetBrightnessValueCompleter::Sync& completer) {
  if (request->index >= lights_.size()) {
    completer.ReplyError(LightError::kInvalidIndex);
    return;
  }
  if (lights_[request->index].SetBrightnessValue(request->value) != ZX_OK) {
    completer.ReplyError(LightError::kFailed);
  } else {
    completer.ReplySuccess();
  }
}

void AmlLight::GetCurrentRgbValue(GetCurrentRgbValueRequestView request,
                                  GetCurrentRgbValueCompleter::Sync& completer) {
  completer.ReplyError(LightError::kNotSupported);
}

void AmlLight::SetRgbValue(SetRgbValueRequestView request, SetRgbValueCompleter::Sync& completer) {
  completer.ReplyError(LightError::kInvalidIndex);
}

void AmlLight::DdkRelease() { delete this; }

zx_status_t AmlLight::Create(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<AmlLight>(new (&ac) AmlLight(parent));
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

zx_status_t AmlLight::Init() {
  zx_status_t status = ZX_OK;
  auto fragment_count = DdkGetFragmentCount();
  if (fragment_count <= 0) {
    return ZX_ERR_INTERNAL;
  }
  struct name_t {
    char name[kNameLength];
  };
  auto names = ddk::GetMetadataArray<name_t>(parent(), DEVICE_METADATA_NAME);
  if (!names.is_ok()) {
    return names.error_value();
  }
  auto configs = ddk::GetMetadataArray<lights_config_t>(parent(), DEVICE_METADATA_LIGHTS);
  if (!configs.is_ok()) {
    return configs.error_value();
  }
  if (names->size() != configs->size()) {
    zxlogf(ERROR, "%s: number of names [%lu] does not match number of configs [%lu]", __func__,
           names->size(), configs->size());
    return ZX_ERR_INTERNAL;
  }

  size_t actual;
  composite_device_fragment_t fragments[fragment_count];
  DdkGetFragments(fragments, fragment_count, &actual);
  if (actual != fragment_count) {
    return ZX_ERR_INTERNAL;
  }

  uint32_t count = 1;
  for (uint32_t i = 0; i < configs->size(); i++) {
    auto* config = &configs.value()[i];
    char* name = names.value()[i].name;

    ddk::GpioProtocolClient gpio(fragments[count].device);
    if (!gpio.is_valid()) {
      zxlogf(ERROR, "%s: could not get gpio protocol: %d", __func__, status);
      return status;
    }
    count++;

    if (config->brightness) {
      ddk::PwmProtocolClient pwm(fragments[count].device);
      if (!pwm.is_valid()) {
        zxlogf(ERROR, "%s: could not get pwm protocol: %d", __func__, status);
        return status;
      }
      count++;
      lights_.emplace_back(name, gpio, pwm);
    } else {
      lights_.emplace_back(name, gpio, std::nullopt);
    }

    if ((status = lights_.back().Init(config->init_on)) != ZX_OK) {
      zxlogf(ERROR, "%s: Could not initialize light", __func__);
      return status;
    }

    // RGB not supported, so not implemented.
  }

  return DdkAdd("gpio-light", DEVICE_ADD_NON_BINDABLE);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlLight::Create;
  return ops;
}();

}  // namespace aml_light

ZIRCON_DRIVER(aml_light, aml_light::driver_ops, "zircon", "0.1");
