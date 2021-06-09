// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ti-ina231.h"

#include <endian.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>

#include <ddktl/fidl.h>
#include <fbl/auto_lock.h>

#include "src/devices/power/drivers/ti-ina231/ti-ina231-bind.h"

namespace {

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
constexpr uint64_t kPowerResolution = (25ULL * 5'120 * kFixedPointFactor) / kCalibrationValue;
static_assert((kPowerResolution * kCalibrationValue) == (25ULL * 5'120 * kFixedPointFactor));

// Divide the bus voltage limit by this to get the alert limit register value.
constexpr uint64_t kMicrovoltsPerBit = 1'250;

constexpr float kMicrovoltsToVolts = 1000.0f * 1000.0f;
constexpr float kVoltsPerBit = kMicrovoltsToVolts / kMicrovoltsPerBit;

}  // namespace

namespace power_sensor {

enum class Ina231Device::Register : uint8_t {
  kConfigurationReg = 0,
  kBusVoltageReg = 2,
  kPowerReg = 3,
  kCalibrationReg = 5,
  kMaskEnableReg = 6,
  kAlertLimitReg = 7,
};

zx_status_t Ina231Device::Create(void* ctx, zx_device_t* parent) {
  ddk::I2cChannel i2c(parent, "i2c");
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "Failed to get I2C protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  Ina231Metadata metadata = {};
  size_t actual = 0;
  zx_status_t status =
      device_get_metadata(parent, DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata), &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to get metadata: %d", status);
    return status;
  }
  if (actual != sizeof(metadata)) {
    zxlogf(ERROR, "Expected %zu bytes of metadata, got %zu", sizeof(metadata), actual);
    return ZX_ERR_NO_RESOURCES;
  }
  if (metadata.shunt_resistance_microohm == 0) {
    zxlogf(ERROR, "Shunt resistance cannot be zero");
    return ZX_ERR_INVALID_ARGS;
  }

  auto dev = std::make_unique<Ina231Device>(parent, metadata.shunt_resistance_microohm, i2c);
  if ((status = dev->Init(metadata)) != ZX_OK) {
    return status;
  }

  zx_device_prop_t props[] = {
      {BIND_POWER_SENSOR_DOMAIN, 0, metadata.power_sensor_domain},
  };
  if ((status = dev->DdkAdd(ddk::DeviceAddArgs("ti-ina231").set_props(props))) != ZX_OK) {
    zxlogf(ERROR, "DdkAdd failed: %d", status);
    return status;
  }

  __UNUSED auto* _ = dev.release();
  return ZX_OK;
}

zx_status_t Ina231Device::PowerSensorConnectServer(zx::channel server) {
  fidl::BindServer(loop_.dispatcher(),
                   fidl::ServerEnd<fuchsia_hardware_power_sensor::Device>(std::move(server)), this);
  return ZX_OK;
}

void Ina231Device::GetPowerWatts(GetPowerWattsRequestView request,
                                 GetPowerWattsCompleter::Sync& completer) {
  zx::status<uint16_t> power_reg;

  {
    fbl::AutoLock lock(&i2c_lock_);
    power_reg = Read16(Register::kPowerReg);
  }

  if (power_reg.is_error()) {
    completer.ReplyError(power_reg.error_value());
    return;
  }

  const uint64_t power = (power_reg.value() * kPowerResolution) / shunt_resistor_uohms_;
  completer.ReplySuccess(static_cast<float>(power) / kFixedPointFactor);
}

void Ina231Device::GetVoltageVolts(GetVoltageVoltsRequestView request,
                                   GetVoltageVoltsCompleter::Sync& completer) {
  zx::status<uint16_t> voltage_reg;

  {
    fbl::AutoLock lock(&i2c_lock_);
    voltage_reg = Read16(Register::kBusVoltageReg);
  }

  if (voltage_reg.is_error()) {
    completer.ReplyError(voltage_reg.error_value());
    return;
  }

  completer.ReplySuccess(static_cast<float>(voltage_reg.value()) / kVoltsPerBit);
}

zx_status_t Ina231Device::Init(const Ina231Metadata& metadata) {
  {
    zx_status_t status = loop_.StartThread("TI INA231 loop thread");
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to start thread: %d", status);
      return status;
    }
  }

  // Keep only the bits that are not defined in the datasheet, and clear the reset bit.
  constexpr uint16_t kConfigurationRegMask = 0x7000;

  fbl::AutoLock lock(&i2c_lock_);

  auto status = Write16(Register::kCalibrationReg, kCalibrationValue);
  if (status.is_error()) {
    return status.error_value();
  }

  if (metadata.alert == Ina231Metadata::kAlertBusUnderVoltage) {
    const uint64_t alert_limit_reg_value = metadata.bus_voltage_limit_microvolt / kMicrovoltsPerBit;
    if (alert_limit_reg_value > UINT16_MAX) {
      zxlogf(ERROR, "Bus voltage limit is out of range");
      return ZX_ERR_OUT_OF_RANGE;
    }

    if ((status = Write16(Register::kAlertLimitReg, alert_limit_reg_value)).is_error()) {
      return status.error_value();
    }
  }

  if ((status = Write16(Register::kMaskEnableReg, metadata.alert)).is_error()) {
    return status.error_value();
  }

  const zx::status<uint16_t> config_status = Read16(Register::kConfigurationReg);
  if (config_status.is_error()) {
    return config_status.error_value();
  }

  const uint16_t metadata_value = metadata.mode | (metadata.shunt_voltage_conversion_time << 3) |
                                  (metadata.bus_voltage_conversion_time << 6) |
                                  (metadata.averages << 9);
  const uint16_t configuration_reg_value =
      (config_status.value() & kConfigurationRegMask) | metadata_value;
  if ((status = Write16(Register::kConfigurationReg, configuration_reg_value)).is_error()) {
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

ZIRCON_DRIVER(ti_ina231, power_sensor::ti_ina231_driver_ops, "ti-ina231", "0.1");
