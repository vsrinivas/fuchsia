// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_AS370_POWER_AS370_POWER_H_
#define SRC_DEVICES_POWER_DRIVERS_AS370_POWER_AS370_POWER_H_

#include <lib/device-protocol/pdev.h>
#include <threads.h>

#include <array>

#include <ddktl/device.h>
#include <ddktl/protocol/i2c.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/powerimpl.h>
#include <soc/as370/as370-power-regs.h>
#include <soc/as370/as370-power.h>

namespace power {

// Describes a voltage regulator
class As370Regulator {
 public:
  As370Regulator(const uint32_t default_voltage, bool enabled)
      : cur_voltage_(default_voltage), enabled_(enabled), default_voltage_(default_voltage) {
    if (!enabled_) {
      cur_voltage_ = 0;
    }
  }

  virtual ~As370Regulator() = default;
  uint32_t cur_voltage() { return cur_voltage_; }
  uint32_t default_voltage() { return default_voltage_; }
  bool enabled() { return enabled_; }

  virtual zx_status_t Enable() = 0;
  virtual zx_status_t Disable() = 0;

  virtual zx_status_t GetSupportedVoltageRange(uint32_t* min_voltage,
                                               uint32_t* max_voltage) const = 0;
  virtual zx_status_t RequestVoltage(uint32_t set_voltage, uint32_t* actual_voltage) = 0;

 protected:
  uint32_t cur_voltage_;
  bool enabled_ = false;

 private:
  const uint32_t default_voltage_;
};

class As370BuckRegulator : public As370Regulator {
 public:
  As370BuckRegulator(const uint32_t enabled, const ddk::I2cProtocolClient& i2c)
      : As370Regulator(BuckRegulatorRegister::kDefaultVoltage, enabled), i2c_(i2c) {}

  zx_status_t Enable() override;
  zx_status_t Disable() override;
  zx_status_t RequestVoltage(uint32_t voltage, uint32_t* actual_voltage) override;

  zx_status_t GetSupportedVoltageRange(uint32_t* min_voltage,
                                       uint32_t* max_voltage) const override {
    *min_voltage = BuckRegulatorRegister::kMinVoltage;
    *max_voltage = BuckRegulatorRegister::kMaxVoltage;
    return ZX_OK;
  }

 private:
  zx_status_t GetVoltageSelector(uint32_t set_voltage, uint32_t* actual_voltage, uint8_t* selector);

  ddk::I2cProtocolClient i2c_;
};

class As370Power;
using As370PowerType = ddk::Device<As370Power, ddk::Unbindable>;

class As370Power : public As370PowerType,
                   public ddk::PowerImplProtocol<As370Power, ddk::base_protocol> {
 public:
  explicit As370Power(zx_device_t* parent) : As370PowerType(parent) {}

  As370Power(const As370Power&) = delete;
  As370Power(As370Power&&) = delete;
  As370Power& operator=(const As370Power&) = delete;
  As370Power& operator=(As370Power&&) = delete;

  virtual ~As370Power() = default;
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

  zx_status_t PowerImplGetPowerDomainStatus(uint32_t index, power_domain_status_t* out_status);
  zx_status_t PowerImplEnablePowerDomain(uint32_t index);
  zx_status_t PowerImplDisablePowerDomain(uint32_t index);
  zx_status_t PowerImplGetSupportedVoltageRange(uint32_t index, uint32_t* min_voltage,
                                                uint32_t* max_voltage);
  zx_status_t PowerImplRequestVoltage(uint32_t index, uint32_t voltage, uint32_t* actual_voltage);
  zx_status_t PowerImplGetCurrentVoltage(uint32_t index, uint32_t* current_voltage);
  zx_status_t PowerImplWritePmicCtrlReg(uint32_t index, uint32_t addr, uint32_t value);
  zx_status_t PowerImplReadPmicCtrlReg(uint32_t index, uint32_t addr, uint32_t* value);
  bool Test();
  zx_status_t Init();

 protected:
  virtual zx_status_t InitializeProtocols(
      ddk::I2cProtocolClient* i2c);  // virtual method overloaded in unit test

 private:
  zx_status_t Bind();
  zx_status_t InitializePowerDomains(const ddk::I2cProtocolClient& i2c);
  std::array<std::unique_ptr<As370Regulator>, kAs370NumPowerDomains> power_domains_;
};

}  // namespace power

#endif  // SRC_DEVICES_POWER_DRIVERS_AS370_POWER_AS370_POWER_H_
