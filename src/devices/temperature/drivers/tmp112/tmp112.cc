// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tmp112.h"

#include <endian.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>

#include <ddktl/fidl.h>

#include "src/devices/temperature/drivers/tmp112/tmp112-bind.h"

namespace temperature {

void Tmp112Device::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void Tmp112Device::DdkRelease() { delete this; }

zx_status_t Tmp112Device::Init() {
  zx_status_t status;

  status = ReadReg(kConfigReg, &config_data_);
  if (status != ZX_OK) {
    LOG_ERROR("Failed to read config: %d\n", status);
    return status;
  }

  // Don't use extended mode
  config_data_ &= ~kConfigExtendedMode;

  // Don't use one-shot mode
  config_data_ &= ~kConfigOneShotMode;

  // Set 12-bit conversion resolution
  config_data_ &= ~kConfigConversionResolutionMask;
  config_data_ |= kConfigConvertResolutionSet12Bit;

  status = WriteReg(kConfigReg, config_data_);
  if (status != ZX_OK) {
    LOG_ERROR("Failed to write config: %d\n", status);
    return status;
  }

  return ZX_OK;
}

zx_status_t Tmp112Device::ReadReg(uint8_t addr, uint16_t* val) {
  return i2c_.ReadSync(addr, reinterpret_cast<uint8_t*>(val), 2);
}

zx_status_t Tmp112Device::WriteReg(uint8_t addr, uint16_t val) {
  uint8_t buf[3] = {
      addr,
      static_cast<uint8_t>((val >> 0) & 0xff),
      static_cast<uint8_t>((val >> 8) & 0xff),
  };
  return i2c_.WriteSync(buf, sizeof(buf));
}

void Tmp112Device::GetTemperatureCelsius(GetTemperatureCelsiusRequestView request,
                                         GetTemperatureCelsiusCompleter::Sync& completer) {
  zx_status_t status;
  uint16_t reg_val;

  status = ReadReg(kTemperatureReg, &reg_val);
  if (status != ZX_OK) {
    LOG_ERROR("Failed to read temperature: %d\n", status);
  }

  completer.Reply(status, RegToTemperatureCelsius(reg_val));
}

float Tmp112Device::RegToTemperatureCelsius(uint16_t reg) {
  // The bottom bits aren't used - shift according to temperature mode
  return static_cast<float>(be16toh(reg) >> kTemperatureNormalModeShift) * kTemperatureResolution;
}

zx_status_t Tmp112Device::Bind(void* ctx, zx_device_t* parent) {
  zx_status_t status;

  // Get I2C protocol
  i2c_protocol_t i2c;
  status = device_get_protocol(parent, ZX_PROTOCOL_I2C, &i2c);
  if (status != ZX_OK) {
    LOG_ERROR("Could not obtain I2C protocol: %d\n", status);
    return status;
  }

  auto dev = std::make_unique<Tmp112Device>(parent, ddk::I2cChannel(&i2c));
  if ((status = dev->Init()) != ZX_OK) {
    return status;
  }

  if ((status = dev->DdkAdd("tmp112")) != ZX_OK) {
    LOG_ERROR("Could not add device: %d\n", status);
    return status;
  }

  // devmgr is now in charge of memory for dev
  __UNUSED auto ptr = dev.release();

  return status;
}

static constexpr zx_driver_ops_t tmp112_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Tmp112Device::Bind;
  return ops;
}();

}  // namespace temperature

ZIRCON_DRIVER(tmp112, temperature::tmp112_driver_ops, "zircon", "0.1");
