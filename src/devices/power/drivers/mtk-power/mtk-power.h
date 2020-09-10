// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_MTK_POWER_MTK_POWER_H_
#define SRC_DEVICES_POWER_DRIVERS_MTK_POWER_MTK_POWER_H_

#include <lib/mmio/mmio.h>
#include <threads.h>

#include <array>

#include <ddktl/device.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/powerimpl.h>
#include <fbl/vector.h>
#include <soc/mt8167/mt8167-power-regs.h>
#include <soc/mt8167/mt8167-power.h>

namespace power {

// Describes a voltage regulator
class MtkRegulator {
 public:
  MtkRegulator(ddk::MmioView pmic_mmio, const uint32_t default_voltage, const uint32_t enable_reg,
               const uint8_t enable_bit)
      : cur_voltage_(default_voltage),
        default_voltage_(default_voltage),
        enable_register_(enable_reg),
        enable_bit_(enable_bit),
        pmic_mmio_(pmic_mmio) {}

  virtual ~MtkRegulator() {}
  uint32_t enable_register() { return enable_register_; }
  uint32_t cur_voltage() { return cur_voltage_; }
  uint32_t default_voltage() { return default_voltage_; }
  uint8_t enable_bit() { return enable_bit_; }
  bool enabled() { return enabled_; }

  zx_status_t Enable();
  zx_status_t Disable();
  virtual zx_status_t GetSupportedVoltageRange(uint32_t* min_voltage,
                                               uint32_t* max_voltage) const = 0;
  virtual zx_status_t RequestVoltage(uint32_t set_voltage, uint32_t* actual_voltage) = 0;

 protected:
  zx_status_t ReadPMICReg(uint32_t reg_addr, uint32_t* reg_value);
  zx_status_t WritePMICReg(uint32_t reg_addr, uint32_t value);
  uint32_t cur_voltage_;

 private:
  const uint32_t default_voltage_;
  const uint32_t enable_register_;
  const uint8_t enable_bit_;
  ddk::MmioView pmic_mmio_;
  bool enabled_ = false;

  void WaitForIdle();
  void WaitForValidClear();
};

class MtkBuckRegulator : public MtkRegulator {
 public:
  MtkBuckRegulator(ddk::MmioView pmic_mmio, const uint32_t enable_reg, const uint8_t enable_bit,
                   const uint32_t voltage_sel_reg, const uint32_t voltage_sel_mask,
                   const uint32_t voltage_sel_shift, const uint32_t buck_voltage_ctrl_reg,
                   const uint32_t buck_voltage_on_reg, const uint32_t min_voltage,
                   const uint32_t max_voltage, const uint32_t step_size)
      : MtkRegulator(pmic_mmio, min_voltage, enable_reg, enable_bit),
        buck_voltage_ctrl_reg_(buck_voltage_ctrl_reg),
        buck_voltage_on_reg_(buck_voltage_on_reg),
        voltage_sel_reg_(voltage_sel_reg),
        voltage_sel_mask_(voltage_sel_mask),
        voltage_sel_shift_(voltage_sel_shift),
        min_voltage_(min_voltage),
        max_voltage_(max_voltage),
        step_size_(step_size) {}

  uint32_t buck_voltage_ctrl_reg() { return buck_voltage_ctrl_reg_; }
  uint32_t buck_voltage_on_reg() { return buck_voltage_on_reg_; }
  uint32_t voltage_sel_reg() { return voltage_sel_reg_; }
  uint32_t voltage_sel_mask() { return voltage_sel_mask_; }
  uint32_t voltage_sel_shift() { return voltage_sel_shift_; }
  uint32_t min_voltage() { return min_voltage_; }
  uint32_t max_voltage() { return max_voltage_; }
  uint32_t step_size() { return step_size_; }
  zx_status_t GetSupportedVoltageRange(uint32_t* min_voltage,
                                       uint32_t* max_voltage) const override {
    *min_voltage = min_voltage_;
    *max_voltage = max_voltage_;
    return ZX_OK;
  }

  zx_status_t RequestVoltage(uint32_t voltage, uint32_t* actual_voltage) override;
  zx_status_t SetVoltageSelReg();

 private:
  zx_status_t GetVoltageSelector(uint32_t set_voltage, uint32_t* actual_voltage,
                                 uint16_t* selector);
  const uint32_t buck_voltage_ctrl_reg_;
  const uint32_t buck_voltage_on_reg_;
  uint32_t voltage_sel_reg_;
  const uint32_t voltage_sel_mask_;
  const uint32_t voltage_sel_shift_;
  const uint32_t min_voltage_;
  const uint32_t max_voltage_;
  const uint32_t step_size_;
};

class MtkLdoRegulator : public MtkRegulator {
 public:
  MtkLdoRegulator(ddk::MmioView pmic_mmio, const uint32_t enable_reg, const uint8_t enable_bit,
                  const uint32_t voltage_sel_reg, const uint32_t voltage_sel_mask,
                  const uint32_t voltage_sel_shift, const fbl::Vector<uint32_t>& supported_voltages)
      : MtkRegulator(pmic_mmio, 0, enable_reg, enable_bit),
        voltage_sel_reg_(voltage_sel_reg),
        voltage_sel_mask_(voltage_sel_mask),
        voltage_sel_shift_(voltage_sel_shift),
        supported_voltages_(supported_voltages) {}
  uint32_t voltage_sel_reg() { return voltage_sel_reg_; }
  uint32_t voltage_sel_mask() { return voltage_sel_mask_; }
  uint32_t voltage_sel_shift() { return voltage_sel_shift_; }
  const fbl::Vector<uint32_t>& supported_voltages() { return supported_voltages_; }
  zx_status_t GetSupportedVoltageRange(uint32_t* min_voltage,
                                       uint32_t* max_voltage) const override {
    if (supported_voltages_.is_empty()) {
      return ZX_ERR_BAD_STATE;
    }
    *min_voltage = *(supported_voltages_.begin());
    *max_voltage = supported_voltages_[supported_voltages_.size() - 1];
    return ZX_OK;
  }
  zx_status_t RequestVoltage(uint32_t voltage, uint32_t* actual_voltage) override;

 private:
  zx_status_t GetVoltageSelector(uint32_t set_voltage, uint32_t* actual_voltage,
                                 uint16_t* selector);
  const uint32_t voltage_sel_reg_;
  const uint32_t voltage_sel_mask_;
  const uint32_t voltage_sel_shift_;
  const fbl::Vector<uint32_t>& supported_voltages_;
};

class MtkFixedRegulator : public MtkRegulator {
 public:
  MtkFixedRegulator(ddk::MmioView pmic_mmio, const uint32_t default_voltage,
                    const uint32_t enable_reg, const uint8_t enable_bit)
      : MtkRegulator(pmic_mmio, default_voltage, enable_reg, enable_bit) {}
  zx_status_t GetSupportedVoltageRange(uint32_t* min_voltage,
                                       uint32_t* max_voltage) const override {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t RequestVoltage(uint32_t voltage, uint32_t* actual_voltage) override {
    return ZX_ERR_NOT_SUPPORTED;
  }
};

class MtkPower;
using MtkPowerType = ddk::Device<MtkPower, ddk::Unbindable>;

class MtkPower : public MtkPowerType, public ddk::PowerImplProtocol<MtkPower, ddk::base_protocol> {
 public:
  explicit MtkPower(zx_device_t* parent, ddk::MmioBuffer mmio)
      : MtkPowerType(parent), pmic_mmio_(std::move(mmio)) {}

  ~MtkPower() = default;
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

 protected:
  void InitializePowerDomains();
  std::array<std::unique_ptr<MtkRegulator>, kMt8167NumPowerDomains> power_domains_;

 private:
  ddk::MmioBuffer pmic_mmio_;

  zx_status_t Bind();
  zx_status_t Init();
  void WaitForIdle();
  void WaitForValidClear();
  zx_status_t ReadPMICReg(uint32_t reg_addr, uint32_t* reg_value);
  zx_status_t WritePMICReg(uint32_t reg_addr, uint32_t value);
};

}  // namespace power

#endif  // SRC_DEVICES_POWER_DRIVERS_MTK_POWER_MTK_POWER_H_
