// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vs680-power.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>

#include <memory>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <hwreg/i2c.h>
#include <soc/vs680/vs680-power.h>

#include "src/devices/power/drivers/vs680-power/vs680-power-bind.h"

namespace {

constexpr uint32_t kStepSizeMicroVolts = 10'000;
constexpr uint32_t kMinVoltageMicroVolts = 600'000;
constexpr uint32_t kMaxVoltageMicroVolts = 1'870'000;

// This is the voltage if VBOOT is set to 1, and depends on the feedback voltage divider. On the
// VS680 EVK board this is set to 0.8V.
constexpr uint32_t kDefaultVoltageMicroVolts = 800'000;

class VSel : public hwreg::I2cRegisterBase<VSel, uint8_t, sizeof(uint8_t)> {
 public:
  static auto Get() { return hwreg::I2cRegisterAddr<VSel>(0x00); }

  DEF_BIT(7, vboot);
  DEF_FIELD(6, 0, voltage);

  auto& set_voltage_microvolts(uint32_t voltage_uv) {
    set_vboot(0);
    set_voltage(static_cast<uint8_t>((voltage_uv - kMinVoltageMicroVolts) / kStepSizeMicroVolts));
    return *this;
  }

  uint32_t voltage_microvolts() const {
    return vboot() ? kDefaultVoltageMicroVolts
                   : (voltage() * kStepSizeMicroVolts) + kMinVoltageMicroVolts;
  }
};

class SysCntrlReg1 : public hwreg::I2cRegisterBase<SysCntrlReg1, uint8_t, sizeof(uint8_t)> {
 public:
  static constexpr uint8_t kGoBitResetThreshold = 50'000 / kStepSizeMicroVolts;

  static auto Get() { return hwreg::I2cRegisterAddr<SysCntrlReg1>(0x01); }

  // This bit must be set before changing the voltage, and will be cleared automatically unless the
  // voltage change is within 50 mV.
  DEF_BIT(6, go_bit);
};

}  // namespace

namespace power {

zx_status_t Vs680Power::Create(void* ctx, zx_device_t* parent) {
  ddk::I2cProtocolClient pmic_i2c(parent, "i2c-pmic");
  if (!pmic_i2c.is_valid()) {
    zxlogf(ERROR, "%s: Failed to get I2C fragment", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<Vs680Power> device(new (&ac) Vs680Power(parent, pmic_i2c));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Failed to allocate device memory", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = device->DdkAdd("vs680-power", DEVICE_ADD_ALLOW_MULTI_COMPOSITE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d", __FILE__, status);
    return status;
  }

  __UNUSED auto* _ = device.release();
  return ZX_OK;
}

zx_status_t Vs680Power::PowerImplGetPowerDomainStatus(uint32_t index,
                                                      power_domain_status_t* out_status) {
  if (index != vs680::kPowerDomainVCpu) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // The VCPU domain is always enabled.
  *out_status = POWER_DOMAIN_STATUS_ENABLED;
  return ZX_OK;
}

zx_status_t Vs680Power::PowerImplEnablePowerDomain(uint32_t index) { return ZX_OK; }
zx_status_t Vs680Power::PowerImplDisablePowerDomain(uint32_t index) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Vs680Power::PowerImplGetSupportedVoltageRange(uint32_t index, uint32_t* out_min,
                                                          uint32_t* out_max) {
  if (index != vs680::kPowerDomainVCpu) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  *out_min = kMinVoltageMicroVolts;
  *out_max = kMaxVoltageMicroVolts;
  return ZX_OK;
}

zx_status_t Vs680Power::PowerImplRequestVoltage(uint32_t index, uint32_t voltage,
                                                uint32_t* out_actual_voltage) {
  if (index != vs680::kPowerDomainVCpu) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (voltage < kMinVoltageMicroVolts || voltage > kMaxVoltageMicroVolts) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if ((voltage - kMinVoltageMicroVolts) % kStepSizeMicroVolts != 0) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  fbl::AutoLock lock(&lock_);

  auto syscntrl = SysCntrlReg1::Get().FromValue(0);
  zx_status_t status = syscntrl.ReadFrom(pmic_i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to read from SysCntrlReg1: %d", __FILE__, status);
    return status;
  }

  if ((status = syscntrl.set_go_bit(1).WriteTo(pmic_i2c_)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to write to SysCntrlReg1: %d", __FILE__, status);
    return status;
  }

  auto vsel = VSel::Get().FromValue(0);
  if ((status = vsel.ReadFrom(pmic_i2c_)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to read from VSel: %d", __FILE__, status);
    return status;
  }

  const uint8_t old_voltage_sel = vsel.voltage();

  if ((status = vsel.set_voltage_microvolts(voltage).WriteTo(pmic_i2c_)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to write to VSel: %d", __FILE__, status);
    return status;
  }

  const uint8_t voltage_sel_delta =
      std::max(old_voltage_sel, vsel.voltage()) - std::min(old_voltage_sel, vsel.voltage());
  if (voltage_sel_delta <= SysCntrlReg1::kGoBitResetThreshold) {
    if ((status = syscntrl.ReadFrom(pmic_i2c_)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to read from SysCntrlReg1: %d", __FILE__, status);
      return status;
    }
    if ((status = syscntrl.set_go_bit(0).WriteTo(pmic_i2c_)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to write to SysCntrlReg1: %d", __FILE__, status);
      return status;
    }
  }

  *out_actual_voltage = voltage;
  return ZX_OK;
}

zx_status_t Vs680Power::PowerImplGetCurrentVoltage(uint32_t index, uint32_t* out_current_voltage) {
  if (index != vs680::kPowerDomainVCpu) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::AutoLock lock(&lock_);

  auto vsel = VSel::Get().FromValue(0);
  zx_status_t status = vsel.ReadFrom(pmic_i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to read from VSel: %d", __FILE__, status);
    return status;
  }

  *out_current_voltage = vsel.voltage_microvolts();
  return ZX_OK;
}

zx_status_t Vs680Power::PowerImplWritePmicCtrlReg(uint32_t index, uint32_t reg_addr,
                                                  uint32_t value) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vs680Power::PowerImplReadPmicCtrlReg(uint32_t index, uint32_t reg_addr,
                                                 uint32_t* out_value) {
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace power

static constexpr zx_driver_ops_t vs680_power_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = power::Vs680Power::Create;
  return ops;
}();

ZIRCON_DRIVER(vs680_power, vs680_power_driver_ops, "zircon", "0.1");
