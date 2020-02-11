// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sgm37603a.h"

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddktl/fidl.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

namespace {

constexpr int64_t kEnableSleepTimeMs = 20;

enum {
  COMPONENT_I2C,
  COMPONENT_GPIO,
  COMPONENT_COUNT,
};

}  // namespace

namespace backlight {

zx_status_t Sgm37603a::Create(void* ctx, zx_device_t* parent) {
  composite_protocol_t composite;

  auto status = device_get_protocol(parent, ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not get ZX_PROTOCOL_COMPOSITE\n", __FILE__);
    return status;
  }

  zx_device_t* components[COMPONENT_COUNT];
  size_t actual;
  composite_get_components(&composite, components, COMPONENT_COUNT, &actual);
  if (actual != COMPONENT_COUNT) {
    zxlogf(ERROR, "%s: could not get our components\n", __FILE__);
    return ZX_ERR_INTERNAL;
  }

  i2c_protocol_t i2c;
  status = device_get_protocol(components[COMPONENT_I2C], ZX_PROTOCOL_I2C, &i2c);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_I2C\n", __FILE__);
    return status;
  }

  gpio_protocol_t reset_gpio;
  status = device_get_protocol(components[COMPONENT_GPIO], ZX_PROTOCOL_GPIO, &reset_gpio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_GPIO\n", __FILE__);
    return status;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<Sgm37603a> device(new (&ac) Sgm37603a(parent, &i2c, &reset_gpio));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Sgm37603a alloc failed\n", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  status = device->SetBacklightState(true, 1.0);
  if (status != ZX_OK) {
    return status;
  }

  if ((status = device->DdkAdd("sgm37603a")) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed\n", __FILE__);
    return status;
  }

  __UNUSED auto* dummy = device.release();

  return ZX_OK;
}

zx_status_t Sgm37603a::EnableBacklight() {
  zx_status_t status = reset_gpio_.ConfigOut(1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to enable backlight driver\n", __FILE__);
    return status;
  }

  zx::nanosleep(zx::deadline_after(zx::msec(kEnableSleepTimeMs)));

  for (size_t i = 0; i < countof(kDefaultRegValues); i++) {
    status = i2c_.WriteSync(kDefaultRegValues[i], sizeof(kDefaultRegValues[i]));
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to configure backlight driver\n", __FILE__);
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t Sgm37603a::DisableBacklight() {
  zx_status_t status = reset_gpio_.ConfigOut(0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to disable backlight driver\n", __FILE__);
    return status;
  }

  return ZX_OK;
}

void Sgm37603a::GetStateNormalized(GetStateNormalizedCompleter::Sync completer) {
  FidlBacklight::State state = {};
  auto status = GetBacklightState(&state.backlight_on, &state.brightness);
  if (status == ZX_OK) {
    completer.ReplySuccess(state);
  } else {
    completer.ReplyError(status);
  }
}

void Sgm37603a::SetStateNormalized(FidlBacklight::State state,
                                   SetStateNormalizedCompleter::Sync completer) {
  auto status = SetBacklightState(state.backlight_on, state.brightness);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void Sgm37603a::GetStateAbsolute(GetStateAbsoluteCompleter::Sync completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Sgm37603a::SetStateAbsolute(FidlBacklight::State state,
                                 SetStateAbsoluteCompleter::Sync completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Sgm37603a::GetMaxAbsoluteBrightness(GetMaxAbsoluteBrightnessCompleter::Sync completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

zx_status_t Sgm37603a::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  FidlBacklight::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

zx_status_t Sgm37603a::GetBacklightState(bool* power, double* brightness) {
  *power = enabled_;
  *brightness = brightness_;
  return ZX_OK;
}

zx_status_t Sgm37603a::SetBacklightState(bool power, double brightness) {
  if (!power) {
    enabled_ = false;
    brightness_ = 0;

    return DisableBacklight();
  } else if (!enabled_) {
    enabled_ = true;

    zx_status_t status = EnableBacklight();
    if (status != ZX_OK) {
      return status;
    }
  }

  brightness = fbl::max(brightness, 0.0);
  brightness = fbl::min(brightness, 1.0);

  uint16_t brightness_value = static_cast<uint16_t>(brightness * kMaxBrightnessRegValue);
  const uint8_t brightness_regs[][2] = {
      {kBrightnessLsb, static_cast<uint8_t>(brightness_value & kBrightnessLsbMask)},
      {kBrightnessMsb, static_cast<uint8_t>(brightness_value >> kBrightnessLsbBits)},
  };

  for (size_t i = 0; i < countof(brightness_regs); i++) {
    zx_status_t status = i2c_.WriteSync(brightness_regs[i], sizeof(brightness_regs[i]));
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to set brightness register\n", __FILE__);
      return status;
    }
  }

  brightness_ = brightness;
  return ZX_OK;
}

}  // namespace backlight

static constexpr zx_driver_ops_t sgm37603a_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = backlight::Sgm37603a::Create;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(sgm37603a, sgm37603a_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_SG_MICRO_SGM37603A),
ZIRCON_DRIVER_END(sgm37603a)
    // clang-format on
