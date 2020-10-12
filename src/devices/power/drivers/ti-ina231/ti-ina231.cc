// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ti-ina231.h"

#include <endian.h>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/composite.h>

#include "src/devices/power/drivers/ti-ina231/ti-ina231-bind.h"

namespace {

constexpr uint16_t kModeMask = 0b11;
constexpr uint16_t kModeContinousShuntAndBus = 0b11;

// Choose 2048 for the calibration value so that the current and shunt voltage registers are the
// same. This results in a power resolution of 6.25 mW with a shunt resistance of 10 milli-ohms.
constexpr uint16_t kCalibrationValue = 2048;

// From the datasheet:
// Current resolution in A/bit = 0.00512 / (calibration value * shunt resistance in ohms)
// Power resolution in W/bit = current resolution in A/bit * 25
//
// We use shunt resistance in micro-ohms, so this becomes:
// Current resolution in A/bit = 5120.0 / (calibration value * shunt resistance in micro-ohms)
// Multiply by kFixedPointFactor to avoid truncation. To get the power in watts, multiply
// kPowerResolution by the power register value, divide by the shunt resistance in micro-ohms, then
// divide again by kFixedPointFactor.
constexpr uint64_t kFixedPointFactor = 1'000;
constexpr uint64_t kPowerResolution = (25 * 5'120 * kFixedPointFactor) / kCalibrationValue;
static_assert((kPowerResolution * kCalibrationValue) == (25 * 5'120 * kFixedPointFactor));

}  // namespace

namespace power_sensor {

enum class Ina231Device::Register : uint8_t {
  kConfigurationReg = 0,
  kPowerReg = 3,
  kCalibrationReg = 5,
};

zx_status_t Ina231Device::Create(void* ctx, zx_device_t* parent) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "Failed to get composite protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  zx_device_t* i2c_device = {};
  composite.GetFragment("i2c", &i2c_device);
  ddk::I2cChannel i2c(i2c_device);
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "Failed to get I2C protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  uint32_t shunt_resistor_uohms = 0;
  size_t actual = 0;
  zx_status_t status = device_get_metadata(parent, DEVICE_METADATA_PRIVATE, &shunt_resistor_uohms,
                                           sizeof(shunt_resistor_uohms), &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to get metadata: %d", status);
    return status;
  }
  if (actual != sizeof(shunt_resistor_uohms)) {
    zxlogf(ERROR, "Expected %zu bytes of metadata, got %zu", sizeof(shunt_resistor_uohms), actual);
    return ZX_ERR_NO_RESOURCES;
  }
  if (shunt_resistor_uohms == 0) {
    zxlogf(ERROR, "Shunt resistance cannot be zero");
    return ZX_ERR_INVALID_ARGS;
  }

  auto dev = std::make_unique<Ina231Device>(parent, shunt_resistor_uohms, i2c);
  if ((status = dev->Init()) != ZX_OK) {
    return status;
  }

  if ((status = dev->DdkAdd("ti-ina231")) != ZX_OK) {
    zxlogf(ERROR, "DdkAdd failed: %d", status);
    return status;
  }

  __UNUSED auto* _ = dev.release();
  return ZX_OK;
}

zx_status_t Ina231Device::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  power_sensor_fidl::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void Ina231Device::GetPowerWatts(GetPowerWattsCompleter::Sync& completer) {
  auto power_reg = Read16(Register::kPowerReg);
  if (power_reg.is_error()) {
    completer.ReplyError(power_reg.error_value());
    return;
  }

  const uint64_t power = (power_reg.value() * kPowerResolution) / shunt_resistor_uohms_;
  completer.ReplySuccess(static_cast<float>(power) / kFixedPointFactor);
}

zx_status_t Ina231Device::Init() {
  auto status = Write16(Register::kCalibrationReg, kCalibrationValue);
  if (status.is_error()) {
    return status.error_value();
  }

  auto value = Read16(Register::kConfigurationReg);
  if (value.is_error()) {
    return value.error_value();
  }

  const uint16_t new_config = (value.value() & ~kModeMask) | kModeContinousShuntAndBus;
  if (value.value() != new_config &&
      (status = Write16(Register::kConfigurationReg, new_config)).is_error()) {
    return status.error_value();
  }

  return ZX_OK;
}

zx::status<uint16_t> Ina231Device::Read16(Register reg) {
  const uint8_t address = static_cast<uint8_t>(reg);
  uint16_t value = 0;
  zx_status_t status = i2c_.WriteReadSync(&address, sizeof(address),
                                          reinterpret_cast<uint8_t*>(&value), sizeof(value));
  if (status != ZX_OK) {
    zxlogf(ERROR, "I2C read failed: %d", status);
    return zx::error(status);
  }
  return zx::ok(betoh16(value));
}

zx::status<> Ina231Device::Write16(Register reg, uint16_t value) {
  uint8_t buffer[] = {static_cast<uint8_t>(reg), static_cast<uint8_t>(value >> 8),
                      static_cast<uint8_t>(value & 0xff)};
  zx_status_t status = i2c_.WriteSync(buffer, sizeof(buffer));
  if (status != ZX_OK) {
    zxlogf(ERROR, "I2C write failed: %d", status);
    return zx::error(status);
  }
  return zx::ok();
}

static constexpr zx_driver_ops_t ti_ina231_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Ina231Device::Create;
  return ops;
}();

}  // namespace power_sensor

ZIRCON_DRIVER(ti_ina231, power_sensor::ti_ina231_driver_ops, "ti-ina231", "0.1")
