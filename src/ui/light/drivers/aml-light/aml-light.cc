// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-light.h"

#include <string.h>

#include <cmath>

#include <ddk/binding.h>
#include <ddk/metadata.h>
#include <ddk/metadata/lights.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/composite.h>
#include <fbl/alloc_checker.h>

namespace aml_light {

namespace {

constexpr double kMaxBrightness = 1.0;
constexpr double kMinBrightness = 0.0;
constexpr uint8_t kMaxBrightness2 = 255;
constexpr uint32_t kPwmPeriodNs = 1250;

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
      .mode_config_buffer = &regular,
      .mode_config_size = sizeof(regular),
  };
  if ((status = pwm_->SetConfig(&config)) != ZX_OK) {
    zxlogf(ERROR, "%s: PWM set config failed", __func__);
    return status;
  }

  value_ = value;
  value2_ = static_cast<uint8_t>(value * kMaxBrightness2);
  return ZX_OK;
}

zx_status_t LightDevice::SetBrightnessValue2(uint8_t value) {
  if (!pwm_.has_value()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = ZX_OK;
  aml_pwm::mode_config regular = {aml_pwm::ON, {}};
  pwm_config_t config = {
      .polarity = false,
      .period_ns = kPwmPeriodNs,
      .duty_cycle = static_cast<float>(value * 100.0 / (kMaxBrightness2 * 1.0)),
      .mode_config_buffer = &regular,
      .mode_config_size = sizeof(regular),
  };
  if ((status = pwm_->SetConfig(&config)) != ZX_OK) {
    zxlogf(ERROR, "%s: PWM set config failed", __func__);
    return status;
  }

  value2_ = value;
  return ZX_OK;
}

void AmlLight::GetNumLights(GetNumLightsCompleter::Sync& completer) {
  completer.Reply(static_cast<uint32_t>(lights_.size()));
}

void AmlLight::GetNumLightGroups(GetNumLightGroupsCompleter::Sync& completer) {
  completer.Reply(0);
}

void AmlLight::GetInfo(uint32_t index, GetInfoCompleter::Sync& completer) {
  if (index >= lights_.size()) {
    completer.ReplyError(LightError::INVALID_INDEX);
    return;
  }
  auto name = lights_[index].GetName();
  completer.ReplySuccess({
      .name = ::fidl::unowned_str(name),
      .capability = lights_[index].GetCapability(),
  });
}

void AmlLight::GetCurrentSimpleValue(uint32_t index,
                                     GetCurrentSimpleValueCompleter::Sync& completer) {
  if (index >= lights_.size()) {
    completer.ReplyError(LightError::INVALID_INDEX);
    return;
  }
  if (lights_[index].GetCapability() == Capability::SIMPLE) {
    completer.ReplySuccess(lights_[index].GetCurrentSimpleValue());
  } else {
    completer.ReplyError(LightError::NOT_SUPPORTED);
  }
}

void AmlLight::SetSimpleValue(uint32_t index, bool value,
                              SetSimpleValueCompleter::Sync& completer) {
  if (index >= lights_.size()) {
    completer.ReplyError(LightError::INVALID_INDEX);
    return;
  }
  if (lights_[index].SetSimpleValue(value) != ZX_OK) {
    completer.ReplyError(LightError::FAILED);
  } else {
    completer.ReplySuccess();
  }
}

void AmlLight::GetCurrentBrightnessValue(uint32_t index,
                                         GetCurrentBrightnessValueCompleter::Sync& completer) {
  if (index >= lights_.size()) {
    completer.ReplyError(LightError::INVALID_INDEX);
    return;
  }
  if (lights_[index].GetCapability() == Capability::BRIGHTNESS) {
    completer.ReplySuccess(lights_[index].GetCurrentBrightnessValue());
  } else {
    completer.ReplyError(LightError::NOT_SUPPORTED);
  }
}

// TODO (rdzhuang): Redundant with GetCurrentBrightnessValue for migration to floating point
void AmlLight::GetCurrentBrightnessValue2(uint32_t index,
                                          GetCurrentBrightnessValue2Completer::Sync& completer) {
  if (index >= lights_.size()) {
    completer.ReplyError(LightError::INVALID_INDEX);
    return;
  }
  if (lights_[index].GetCapability() == Capability::BRIGHTNESS) {
    completer.ReplySuccess(lights_[index].GetCurrentBrightnessValue2());
  } else {
    completer.ReplyError(LightError::NOT_SUPPORTED);
  }
}

void AmlLight::SetBrightnessValue(uint32_t index, double value,
                                  SetBrightnessValueCompleter::Sync& completer) {
  if (index >= lights_.size()) {
    completer.ReplyError(LightError::INVALID_INDEX);
    return;
  }
  if (lights_[index].SetBrightnessValue(value) != ZX_OK) {
    completer.ReplyError(LightError::FAILED);
  } else {
    completer.ReplySuccess();
  }
}

// TODO (rdzhuang): Redundant with SetCurrentBrightnessValue for migration to floating point
void AmlLight::SetBrightnessValue2(uint32_t index, uint8_t value,
                                   SetBrightnessValue2Completer::Sync& completer) {
  if (index >= lights_.size()) {
    completer.ReplyError(LightError::INVALID_INDEX);
    return;
  }
  if (lights_[index].SetBrightnessValue2(value) != ZX_OK) {
    completer.ReplyError(LightError::FAILED);
  } else {
    completer.ReplySuccess();
  }
}

void AmlLight::GetCurrentRgbValue(uint32_t index, GetCurrentRgbValueCompleter::Sync& completer) {
  completer.ReplyError(LightError::NOT_SUPPORTED);
}

void AmlLight::SetRgbValue(uint32_t index, Rgb value, SetRgbValueCompleter::Sync& completer) {
  completer.ReplyError(LightError::INVALID_INDEX);
}

zx_status_t AmlLight::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  Light::Dispatch(this, msg, &transaction);
  return transaction.Status();
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

  ddk::CompositeProtocolClient composite(parent());
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s: Could not get composite protocol", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto fragment_count = composite.GetFragmentCount();
  if (fragment_count <= 0) {
    return ZX_ERR_INTERNAL;
  }

  fbl::AllocChecker ac;
  size_t metadata_size, actual;
  if ((status = device_get_metadata_size(parent(), DEVICE_METADATA_NAME, &metadata_size)) !=
      ZX_OK) {
    zxlogf(ERROR, "%s: couldn't get metadata size", __func__);
    return status;
  }
  auto names = fbl::Array<char>(new (&ac) char[metadata_size], metadata_size);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  if ((status = device_get_metadata(parent(), DEVICE_METADATA_NAME, names.data(), metadata_size,
                                    &actual)) != ZX_OK) {
    return status;
  }
  if ((actual != metadata_size) || (actual % kNameLength != 0)) {
    zxlogf(ERROR, "%s: wrong metadata size", __func__);
    return ZX_ERR_INVALID_ARGS;
  }
  uint32_t led_count = static_cast<uint32_t>(metadata_size / kNameLength);

  if (((status = device_get_metadata_size(parent(), DEVICE_METADATA_LIGHTS, &metadata_size)) !=
       ZX_OK) ||
      (metadata_size != led_count * sizeof(lights_config_t))) {
    zxlogf(ERROR, "%s: get metdata size failed", __func__);
    return status;
  }
  auto configs = fbl::Array<lights_config_t>(new (&ac) lights_config_t[led_count], led_count);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  if ((status = device_get_metadata(parent(), DEVICE_METADATA_LIGHTS, configs.data(),
                                    configs.size() * sizeof(lights_config_t), &actual)) != ZX_OK) {
    return status;
  }
  if ((actual != metadata_size) || (actual % sizeof(lights_config_t) != 0)) {
    zxlogf(ERROR, "%s: wrong metadata size", __func__);
    return ZX_ERR_INVALID_ARGS;
  }

  zx_device_t* fragments[fragment_count];
  composite.GetFragments(fragments, fragment_count, &actual);
  if (actual != fragment_count) {
    return ZX_ERR_INTERNAL;
  }

  uint32_t count = 1;
  for (uint32_t i = 0; i < led_count; i++) {
    auto* config = &configs[i];

    ddk::GpioProtocolClient gpio(fragments[count]);
    if (!gpio.is_valid()) {
      zxlogf(ERROR, "%s: could not get gpio protocol: %d", __func__, status);
      return status;
    }
    count++;

    if (config->brightness) {
      ddk::PwmProtocolClient pwm(fragments[count]);
      if (!pwm.is_valid()) {
        zxlogf(ERROR, "%s: could not get pwm protocol: %d", __func__, status);
        return status;
      }
      count++;
      if (i * kNameLength >= names.size()) {
        zxlogf(ERROR, "%s: name buffer overflow!", __func__);
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      lights_.emplace_back(&names[i * kNameLength], gpio, pwm);
    } else {
      if (i * kNameLength >= names.size()) {
        zxlogf(ERROR, "%s: name buffer overflow!", __func__);
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      lights_.emplace_back(&names[i * kNameLength], gpio, std::nullopt);
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

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_light, aml_light::driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_GPIO_LIGHT),
ZIRCON_DRIVER_END(aml_light)
    // clang-format on
