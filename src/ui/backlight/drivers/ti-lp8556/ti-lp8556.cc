// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "ti-lp8556.h"

#include <lib/device-protocol/i2c.h>
#include <lib/device-protocol/pdev.h>

#include <algorithm>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddktl/fidl.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

namespace ti {

enum {
  FRAGMENT_PDEV,
  FRAGMENT_I2C,
  FRAGMENT_COUNT,
};

void Lp8556Device::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void Lp8556Device::DdkRelease() { delete this; }

zx_status_t Lp8556Device::GetBacklightState(bool* power, double* brightness) {
  *power = power_;
  *brightness = brightness_;
  return ZX_OK;
}

zx_status_t Lp8556Device::SetBacklightState(bool power, double brightness) {
  brightness = std::max(brightness, 0.0);
  brightness = std::min(brightness, 1.0);

  if (brightness != brightness_) {
    uint16_t brightness_reg_value = static_cast<uint16_t>(brightness * kBrightnessRegMaxValue);

    // LSB should be updated before MSB. Writing to MSB triggers the brightness change.
    uint8_t buf[2];
    buf[0] = kBacklightBrightnessLsbReg;
    buf[1] = static_cast<uint8_t>(brightness_reg_value & kBrightnessLsbMask);
    zx_status_t status = i2c_.WriteSync(buf, sizeof(buf));
    if (status != ZX_OK) {
      LOG_ERROR("Failed to set brightness LSB register\n");
      return status;
    }

    uint8_t msb_reg_value;
    status = i2c_.ReadSync(kBacklightBrightnessMsbReg, &msb_reg_value, 1);
    if (status != ZX_OK) {
      LOG_ERROR("Failed to get brightness MSB register\n");
      return status;
    }

    // The low 4-bits contain the brightness MSB. Keep the remaining bits unchanged.
    msb_reg_value &= static_cast<uint8_t>(~kBrightnessMsbByteMask);
    msb_reg_value |=
        (static_cast<uint8_t>((brightness_reg_value & kBrightnessMsbMask) >> kBrightnessMsbShift));

    buf[0] = kBacklightBrightnessMsbReg;
    buf[1] = msb_reg_value;
    status = i2c_.WriteSync(buf, sizeof(buf));
    if (status != ZX_OK) {
      LOG_ERROR("Failed to set brightness MSB register\n");
      return status;
    }

    auto persistent_brightness = BrightnessStickyReg::Get().ReadFrom(&mmio_);
    persistent_brightness.set_brightness(brightness_reg_value & kBrightnessRegMask);
    persistent_brightness.set_is_valid(1);
    persistent_brightness.WriteTo(&mmio_);
  }

  if (power != power_) {
    uint8_t buf[2];
    buf[0] = kDeviceControlReg;
    buf[1] = power ? kBacklightOn : kBacklightOff;
    zx_status_t status = i2c_.WriteSync(buf, sizeof(buf));
    if (status != ZX_OK) {
      LOG_ERROR("Failed to set device control register\n");
      return status;
    }

    if (power) {
      for (size_t i = 0; i < init_registers_size_; i += 2) {
        if ((status = i2c_.WriteSync(&init_registers_[i], 2)) != ZX_OK) {
          LOG_ERROR("Failed to set register 0x%02x: %d\n", init_registers_[i], status);
          return status;
        }
      }

      buf[0] = kCfg2Reg;
      buf[1] = cfg2_;
      status = i2c_.WriteSync(buf, sizeof(buf));
      if (status != ZX_OK) {
        LOG_ERROR("Failed to set cfg2 register\n");
        return status;
      }
    }
  }

  // update internal values
  power_ = power;
  brightness_ = brightness;
  return ZX_OK;
}

void Lp8556Device::GetStateNormalized(GetStateNormalizedCompleter::Sync completer) {
  FidlBacklight::State state = {};
  auto status = GetBacklightState(&state.backlight_on, &state.brightness);
  if (status == ZX_OK) {
    completer.ReplySuccess(state);
  } else {
    completer.ReplyError(status);
  }
}

void Lp8556Device::SetStateNormalized(FidlBacklight::State state,
                                      SetStateNormalizedCompleter::Sync completer) {
  auto status = SetBacklightState(state.backlight_on, state.brightness);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void Lp8556Device::GetStateAbsolute(GetStateAbsoluteCompleter::Sync completer) {
  if (!max_absolute_brightness_nits_.has_value()) {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  if (scale_ != calibrated_scale_) {
    LOG_ERROR("Can't get absolute state with non-calibrated current scale\n");
    completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  FidlBacklight::State state = {};
  auto status = GetBacklightState(&state.backlight_on, &state.brightness);
  if (status == ZX_OK) {
    state.brightness *= max_absolute_brightness_nits_.value();
    completer.ReplySuccess(state);
  } else {
    completer.ReplyError(status);
  }
}

void Lp8556Device::SetStateAbsolute(FidlBacklight::State state,
                                    SetStateAbsoluteCompleter::Sync completer) {
  if (!max_absolute_brightness_nits_.has_value()) {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  // Restore the calibrated current scale that the bootloader set. This and the maximum brightness
  // are the only values we have that can be used to set the absolute brightness in nits.
  auto status = SetCurrentScale(calibrated_scale_);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }

  status = SetBacklightState(state.backlight_on,
                             state.brightness / max_absolute_brightness_nits_.value());
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void Lp8556Device::GetMaxAbsoluteBrightness(GetMaxAbsoluteBrightnessCompleter::Sync completer) {
  if (max_absolute_brightness_nits_.has_value()) {
    completer.ReplySuccess(max_absolute_brightness_nits_.value());
  } else {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
}

void Lp8556Device::SetNormalizedBrightnessScale(
    double scale, SetNormalizedBrightnessScaleCompleter::Sync completer) {
  scale = std::clamp(scale, 0.0, 1.0);

  zx_status_t status = SetCurrentScale(static_cast<uint16_t>(scale * kBrightnessRegMaxValue));
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

void Lp8556Device::GetNormalizedBrightnessScale(
    GetNormalizedBrightnessScaleCompleter::Sync completer) {
  completer.ReplySuccess(static_cast<double>(scale_) / kBrightnessRegMaxValue);
}

zx_status_t Lp8556Device::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  FidlBacklight::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

zx_status_t Lp8556Device::Init() {
  double brightness_nits = 0.0;
  size_t actual;
  zx_status_t status = device_get_metadata(parent(), DEVICE_METADATA_BACKLIGHT_MAX_BRIGHTNESS_NITS,
                                           &brightness_nits, sizeof(brightness_nits), &actual);
  if (status == ZX_OK && actual == sizeof(brightness_nits)) {
    SetMaxAbsoluteBrightnessNits(brightness_nits);
  }

  status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, init_registers_,
                               sizeof(init_registers_), &actual);
  // Supplying this metadata is optional.
  if (status == ZX_OK) {
    if (actual % (2 * sizeof(uint8_t)) != 0) {
      LOG_ERROR("Register metadata is invalid\n");
      return ZX_ERR_INVALID_ARGS;
    } else if (actual > sizeof(init_registers_)) {
      LOG_ERROR("Too many registers specified in metadata\n");
      return ZX_ERR_OUT_OF_RANGE;
    }

    init_registers_size_ = actual;

    for (size_t i = 0; i < init_registers_size_; i += 2) {
      if ((status = i2c_.WriteSync(&init_registers_[i], 2)) != ZX_OK) {
        LOG_ERROR("Failed to set register 0x%02x: %d\n", init_registers_[i], status);
        return status;
      }
    }
  }

  auto persistent_brightness = BrightnessStickyReg::Get().ReadFrom(&mmio_);

  if (persistent_brightness.is_valid()) {
    double brightness =
        static_cast<double>(persistent_brightness.brightness()) / kBrightnessRegMaxValue;

    if ((status = SetBacklightState(brightness > 0, brightness)) != ZX_OK) {
      LOG_ERROR("Could not set persistent brightness value: %f\n", brightness);
    } else {
      LOG_INFO("Successfully set persistent brightness value: %f\n", brightness);
    }
  }

  if ((i2c_.ReadSync(kCfg2Reg, &cfg2_, 1) != ZX_OK) || (cfg2_ == 0)) {
    cfg2_ = kCfg2Default;
  }

  uint8_t buf[2];
  if ((i2c_.ReadSync(kCurrentLsbReg, buf, sizeof(buf))) != ZX_OK) {
    LOG_ERROR("Could not read current scale value: %d\n", status);
    return status;
  }
  scale_ = static_cast<uint16_t>(buf[0] | (buf[1] << kBrightnessMsbShift)) & kBrightnessRegMask;
  calibrated_scale_ = scale_;

  return ZX_OK;
}

zx_status_t Lp8556Device::SetCurrentScale(uint16_t scale) {
  scale &= kBrightnessRegMask;

  if (scale == scale_) {
    return ZX_OK;
  }

  uint8_t msb_reg_value;
  zx_status_t status = i2c_.ReadSync(kCurrentMsbReg, &msb_reg_value, sizeof(msb_reg_value));
  if (status != ZX_OK) {
    LOG_ERROR("Failed to get current scale register: %d", status);
    return status;
  }
  msb_reg_value &= ~kBrightnessMsbByteMask;

  const uint8_t buf[] = {
      kCurrentLsbReg,
      static_cast<uint8_t>(scale & kBrightnessLsbMask),
      static_cast<uint8_t>(msb_reg_value | (scale >> kBrightnessMsbShift)),
  };
  if ((status = i2c_.WriteSync(buf, sizeof(buf))) != ZX_OK) {
    LOG_ERROR("Failed to set current scale register: %d", status);
    return status;
  }

  scale_ = scale;
  return ZX_OK;
}

zx_status_t ti_lp8556_bind(void* ctx, zx_device_t* parent) {
  composite_protocol_t composite;

  auto status = device_get_protocol(parent, ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    LOG_ERROR("Could not get composite protocol\n");
    return status;
  }

  zx_device_t* fragments[FRAGMENT_COUNT];
  size_t actual;
  composite_get_fragments(&composite, fragments, FRAGMENT_COUNT, &actual);
  if (actual != FRAGMENT_COUNT) {
    LOG_ERROR("Could not get fragments\n");
    return ZX_ERR_INTERNAL;
  }

  // Get platform device protocol
  ddk::PDev pdev(fragments[FRAGMENT_PDEV]);
  if (!pdev.is_valid()) {
    LOG_ERROR("Could not get PDEV protocol\n");
    return ZX_ERR_NO_RESOURCES;
  }

  // Map MMIO
  std::optional<ddk::MmioBuffer> mmio;
  status = pdev.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    LOG_ERROR("Could not map mmio %d\n", status);
    return status;
  }

  // Obtain I2C protocol needed to control backlight
  i2c_protocol_t i2c;
  status = device_get_protocol(fragments[FRAGMENT_I2C], ZX_PROTOCOL_I2C, &i2c);
  if (status != ZX_OK) {
    LOG_ERROR("Could not obtain I2C protocol\n");
    return status;
  }
  ddk::I2cChannel i2c_channel(&i2c);

  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<ti::Lp8556Device>(&ac, parent, std::move(i2c_channel),
                                                        *std::move(mmio));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = dev->Init()) != ZX_OK) {
    return status;
  }

  status = dev->DdkAdd("ti-lp8556");
  if (status != ZX_OK) {
    LOG_ERROR("Could not add device\n");
    return status;
  }

  // devmgr is now in charge of memory for dev
  __UNUSED auto ptr = dev.release();

  return status;
}

static constexpr zx_driver_ops_t ti_lp8556_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = ti_lp8556_bind;
  return ops;
}();

}  // namespace ti

// clang-format off
ZIRCON_DRIVER_BEGIN(ti_lp8556, ti::ti_lp8556_driver_ops, "TI-LP8556", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TI),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_TI_LP8556),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TI_BACKLIGHT),
ZIRCON_DRIVER_END(ti_lp8556)
    // clang-format on
