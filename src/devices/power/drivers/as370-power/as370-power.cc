// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370-power.h"

#include <lib/device-protocol/pdev.h>

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/protocol/composite.h>
#include <fbl/alloc_checker.h>
#include <soc/as370/as370-power.h>

#include "src/devices/power/drivers/as370-power/as370_power-bind.h"

namespace {
enum As370RegulatorType { BUCK = 1 };

struct As370PowerDomainParams {
  uint8_t type = 0;
  bool enabled = false;
};

As370PowerDomainParams kAs370PowerDomainParams[kAs370NumPowerDomains] = {
    [kBuckSoC] =
        {
            .type = BUCK,
            .enabled = true,
        },
};
}  // namespace

namespace power {

static bool run_test(void* ctx, zx_device_t* parent, zx_handle_t channel) {
  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<As370Power>(new (&ac) As370Power(parent));
  if (!ac.check()) {
    return false;
  }
  auto status = dev->Init();
  if (status != ZX_OK) {
    return false;
  }
  return dev->Test();
}

bool As370Power::Test() {
  zx_status_t status;
  uint32_t curr_voltage = 0;

  power_domain_status_t regulator_status = POWER_DOMAIN_STATUS_DISABLED;

  // Testing the Buck regulator
  // Default status - enabled
  status = PowerImplGetPowerDomainStatus(kBuckSoC, &regulator_status);
  if ((status != ZX_OK) || (regulator_status != POWER_DOMAIN_STATUS_ENABLED)) {
    zxlogf(ERROR, "Get power domain status kBuckSoC failed : %d", status);
    return false;
  }

  // Get Range
  uint32_t min_voltage = 0;
  uint32_t max_voltage = 0;
  status = PowerImplGetSupportedVoltageRange(kBuckSoC, &min_voltage, &max_voltage);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Get supported voltage range kBuckSoC failed : %d", status);
    return false;
  }

  // Check default voltage
  status = PowerImplGetCurrentVoltage(kBuckSoC, &curr_voltage);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Get current voltage kBuckSoC failed : %d", status);
    return false;
  }

#if 0
  // Note: Disable regulator seems to not work at hardware level
  status = PowerImplDisablePowerDomain(kBuckSoC);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Disable power domain kBuckSoC failed : %d", status);
    return false;
  }

  // Setting to minimum turns off the SoC.
  uint32_t set_voltage = min_voltage;
  status = PowerImplRequestVoltage(kBuckSoC, set_voltage, &set_voltage);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Request voltage kBuckSoC failed : %d", status);
    return false;
  }
#endif

  zxlogf(INFO, "as370-power test passed");

  return true;
}

zx_status_t As370BuckRegulator::Enable() {
  if (enabled_) {
    return ZX_OK;
  }

  auto buck_reg = BuckRegulatorRegister::Get().FromValue(0);
  zx_status_t status = buck_reg.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Reading PMIC reg failed: %d", __FUNCTION__, status);
    return status;
  }

  status = buck_reg.set_buck_enable(1).WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Writing PMIC reg failed: %d", __FUNCTION__, status);
    return status;
  }

  enabled_ = true;
  return ZX_OK;
}

zx_status_t As370BuckRegulator::Disable() {
  if (!enabled_) {
    return ZX_OK;
  }
  auto buck_reg = BuckRegulatorRegister::Get().FromValue(0);
  zx_status_t status = buck_reg.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Reading PMIC reg failed: %d", __FUNCTION__, status);
    return status;
  }

  status = buck_reg.set_buck_enable(0).WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Writing PMIC reg failed: %d", __FUNCTION__, status);
    return status;
  }

  enabled_ = false;
  return ZX_OK;
}

zx_status_t As370BuckRegulator::GetVoltageSelector(uint32_t set_voltage, uint32_t* actual_voltage,
                                                   uint8_t* selector) {
  if (set_voltage < BuckRegulatorRegister::kMinVoltage) {
    zxlogf(ERROR, "%s Voltage :%x is not a supported voltage", __FUNCTION__, set_voltage);
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (set_voltage > BuckRegulatorRegister::kMaxVoltage) {
    zxlogf(ERROR, "%s Voltage :%x is not a supported voltage", __FUNCTION__, set_voltage);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Convert set voltage to regulator value
  uint8_t regulator_value = static_cast<uint8_t>(
      (set_voltage - BuckRegulatorRegister::kMinVoltage) / BuckRegulatorRegister::kStepSize);

  *actual_voltage =
      BuckRegulatorRegister::kMinVoltage + (regulator_value * BuckRegulatorRegister::kStepSize);
  *selector = regulator_value;

  return ZX_OK;
}

zx_status_t As370BuckRegulator::RequestVoltage(uint32_t voltage, uint32_t* actual_voltage) {
  uint8_t selector = 0;
  zx_status_t status = GetVoltageSelector(voltage, actual_voltage, &selector);
  if (status != ZX_OK) {
    return status;
  }

  if (cur_voltage_ == *actual_voltage) {
    return ZX_OK;
  }

  auto buck_reg = BuckRegulatorRegister::Get().FromValue(0);
  status = buck_reg.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Reading PMIC reg failed: %d", __FUNCTION__, status);
    return status;
  }

  status = buck_reg.set_voltage(selector).WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Writing PMIC reg failed: %d", __FUNCTION__, status);
    return status;
  }

  cur_voltage_ = *actual_voltage;
  return ZX_OK;
}

zx_status_t As370Power::PowerImplWritePmicCtrlReg(uint32_t index, uint32_t addr, uint32_t value) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370Power::PowerImplReadPmicCtrlReg(uint32_t index, uint32_t addr, uint32_t* value) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370Power::PowerImplDisablePowerDomain(uint32_t index) {
  if (index >= kAs370NumPowerDomains) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return power_domains_[index]->Disable();
}

zx_status_t As370Power::PowerImplEnablePowerDomain(uint32_t index) {
  if (index >= kAs370NumPowerDomains) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return power_domains_[index]->Enable();
}

zx_status_t As370Power::PowerImplGetPowerDomainStatus(uint32_t index,
                                                      power_domain_status_t* out_status) {
  if (index >= kAs370NumPowerDomains) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  *out_status =
      power_domains_[index]->enabled() ? POWER_DOMAIN_STATUS_ENABLED : POWER_DOMAIN_STATUS_DISABLED;
  return ZX_OK;
}

zx_status_t As370Power::PowerImplGetSupportedVoltageRange(uint32_t index, uint32_t* min_voltage,
                                                          uint32_t* max_voltage) {
  if (index >= kAs370NumPowerDomains) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return power_domains_[index]->GetSupportedVoltageRange(min_voltage, max_voltage);
}

zx_status_t As370Power::PowerImplRequestVoltage(uint32_t index, uint32_t voltage,
                                                uint32_t* actual_voltage) {
  if (index >= kAs370NumPowerDomains) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return power_domains_[index]->RequestVoltage(voltage, actual_voltage);
}

zx_status_t As370Power::PowerImplGetCurrentVoltage(uint32_t index, uint32_t* current_voltage) {
  if (index >= kAs370NumPowerDomains) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  *current_voltage = power_domains_[index]->cur_voltage();
  return ZX_OK;
}

void As370Power::DdkRelease() { delete this; }

void As370Power::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

zx_status_t As370Power::InitializePowerDomains(const ddk::I2cProtocolClient& i2c) {
  for (size_t i = 0; i < kAs370NumPowerDomains; i++) {
    auto& domain_params = kAs370PowerDomainParams[i];
    if (domain_params.type == BUCK) {
      power_domains_[i] = std::make_unique<As370BuckRegulator>(domain_params.enabled, i2c);
    } else {
      zxlogf(ERROR, "Invalid power domain type :%d", domain_params.type);
      return ZX_ERR_INTERNAL;
    }
  }
  return ZX_OK;
}

zx_status_t As370Power::InitializeProtocols(ddk::I2cProtocolClient* i2c) {
  // Get I2C protocol.
  ddk::CompositeProtocolClient composite(parent());
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s: Get ZX_PROTOCOL_COMPOSITE failed", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  *i2c = ddk::I2cProtocolClient(composite, "i2c");
  if (!i2c->is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_I2C not found", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  return ZX_OK;
}

zx_status_t As370Power::Init() {
  ddk::I2cProtocolClient i2c;

  zx_status_t status = InitializeProtocols(&i2c);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to initialize protocols");
    return status;
  }

  status = InitializePowerDomains(i2c);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to initialize power domains");
    return status;
  }
  return ZX_OK;
}

zx_status_t As370Power::Bind() {
  zx_status_t status = DdkAdd("as370-power", DEVICE_ADD_ALLOW_MULTI_COMPOSITE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s DdkAdd failed: %d", __FUNCTION__, status);
  }

  return status;
}

zx_status_t As370Power::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status = ZX_OK;

  // get and pass i2c handle
  auto dev = std::make_unique<As370Power>(parent);

  if ((status = dev->Init()) != ZX_OK) {
    return status;
  }

  if ((status = dev->Bind()) != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();
  return ZX_OK;
}

static constexpr zx_driver_ops_t as370_power_driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = As370Power::Create;
  driver_ops.run_unit_tests = run_test;
  return driver_ops;
}();

}  // namespace power

// clang-format off
ZIRCON_DRIVER(as370_power, power::as370_power_driver_ops, "zircon", "0.1");

//clang-format on
