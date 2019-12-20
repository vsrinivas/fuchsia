// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-light.h"

#include <string.h>

#include <ddk/binding.h>
#include <ddk/metadata.h>
#include <ddk/metadata/lights.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/composite.h>
#include <fbl/alloc_checker.h>

namespace aml_light {

namespace {

constexpr uint8_t kMaxBrightness = 255;
constexpr uint8_t kMinBrightness = 0;

}  // namespace

zx_status_t LightDevice::Init(bool init_on) {
  zx_status_t status = ZX_OK;
  if (pwm_.has_value() && ((status = pwm_->Enable()) != ZX_OK)) {
    return status;
  }
  return SetSimpleValue(init_on ? kMaxBrightness : kMinBrightness);
}

bool LightDevice::HasCapability(Capability capability) const {
  switch (capability) {
    case Capability::BRIGHTNESS:
      return pwm_.has_value();
    case Capability::RGB:
    default:
      return false;
  }
}

zx_status_t LightDevice::SetSimpleValue(uint8_t value) {
  zx_status_t status = ZX_OK;
  if (pwm_.has_value()) {
    aml_pwm::mode_config regular = {aml_pwm::ON, {}};
    pwm_config_t config = {
        .polarity = false,
        .period_ns = 1250,
        .duty_cycle = static_cast<float>(value * 100.0 / (kMaxBrightness * 1.0)),
        .mode_config_buffer = &regular,
        .mode_config_size = sizeof(regular),
    };
    if ((status = pwm_->SetConfig(&config)) != ZX_OK) {
      zxlogf(ERROR, "%s: PWM set config failed\n", __func__);
      return status;
    }

  } else {
    if ((status = gpio_.Write(value)) != ZX_OK) {
      zxlogf(ERROR, "%s: GPIO write failed\n", __func__);
      return status;
    }
  }

  value_ = value;
  return ZX_OK;
}

void AmlLight::GetName(uint32_t index, GetNameCompleter::Sync completer) {
  if (index >= lights_.size()) {
    return completer.Reply(ZX_ERR_OUT_OF_RANGE, ::fidl::StringView(nullptr, 0));
  }
  auto name = lights_[index].GetName();
  return completer.Reply(ZX_OK, ::fidl::StringView(&name[0], name.size() + 1));
}

void AmlLight::GetCount(GetCountCompleter::Sync completer) {
  return completer.Reply(static_cast<uint32_t>(lights_.size()));
}

void AmlLight::HasCapability(uint32_t index, Capability capability,
                             HasCapabilityCompleter::Sync completer) {
  if (index >= lights_.size()) {
    return completer.Reply(ZX_ERR_OUT_OF_RANGE, false);
  }
  return completer.Reply(ZX_OK, lights_[index].HasCapability(capability));
}

void AmlLight::GetSimpleValue(uint32_t index, GetSimpleValueCompleter::Sync completer) {
  if (index >= lights_.size()) {
    return completer.Reply(ZX_ERR_OUT_OF_RANGE, 0);
  }
  return completer.Reply(ZX_OK, lights_[index].GetSimpleValue());
}

void AmlLight::SetSimpleValue(uint32_t index, uint8_t value,
                              SetSimpleValueCompleter::Sync completer) {
  if (index >= lights_.size()) {
    return completer.Reply(ZX_ERR_OUT_OF_RANGE);
  }
  return completer.Reply(lights_[index].SetSimpleValue(value));
}

void AmlLight::GetRgbValue(uint32_t index, GetRgbValueCompleter::Sync completer) {
  return completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
}

void AmlLight::SetRgbValue(uint32_t index, Rgb value, SetRgbValueCompleter::Sync completer) {
  return completer.Reply(ZX_ERR_NOT_SUPPORTED);
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
    zxlogf(ERROR, "%s: Could not get composite protocol\n", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto component_count = composite.GetComponentCount();
  if (component_count <= 0) {
    return ZX_ERR_INTERNAL;
  }

  fbl::AllocChecker ac;
  size_t metadata_size, actual;
  if ((status = device_get_metadata_size(parent(), DEVICE_METADATA_NAME, &metadata_size)) !=
      ZX_OK) {
    zxlogf(ERROR, "%s: couldn't get metadata size\n", __func__);
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
    zxlogf(ERROR, "%s: wrong metadata size\n", __func__);
    return ZX_ERR_INVALID_ARGS;
  }
  uint32_t led_count = static_cast<uint32_t>(metadata_size / kNameLength);

  if (((status = device_get_metadata_size(parent(), DEVICE_METADATA_LIGHTS, &metadata_size)) !=
       ZX_OK) ||
      (metadata_size != led_count * sizeof(lights_config_t))) {
    zxlogf(ERROR, "%s: get metdata size failed\n", __func__);
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
    zxlogf(ERROR, "%s: wrong metadata size\n", __func__);
    return ZX_ERR_INVALID_ARGS;
  }

  zx_device_t* components[component_count];
  composite.GetComponents(components, component_count, &actual);
  if (actual != component_count) {
    return ZX_ERR_INTERNAL;
  }

  uint32_t count = 1;
  for (uint32_t i = 0; i < led_count; i++) {
    auto* config = &configs[i];

    ddk::GpioProtocolClient gpio(components[count]);
    if (!gpio.is_valid()) {
      zxlogf(ERROR, "%s: could not get gpio protocol: %d\n", __func__, status);
      return status;
    }
    count++;

    if (config->brightness) {
      ddk::PwmProtocolClient pwm(components[count]);
      if (!pwm.is_valid()) {
        zxlogf(ERROR, "%s: could not get pwm protocol: %d\n", __func__, status);
        return status;
      }
      count++;
      if (i * kNameLength >= names.size()) {
        zxlogf(ERROR, "%s: name buffer overflow!\n", __func__);
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      lights_.emplace_back(&names[i * kNameLength], gpio, pwm);
    } else {
      if (i * kNameLength >= names.size()) {
        zxlogf(ERROR, "%s: name buffer overflow!\n", __func__);
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      lights_.emplace_back(&names[i * kNameLength], gpio, std::nullopt);
    }

    if ((status = lights_.back().Init(config->init_on)) != ZX_OK) {
      zxlogf(ERROR, "%s: Could not initialize light\n", __func__);
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
