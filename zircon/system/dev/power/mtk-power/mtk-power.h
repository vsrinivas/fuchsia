// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <array>
#include <ddktl/device.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/powerimpl.h>
#include <fbl/vector.h>
#include <lib/mmio/mmio.h>
#include <soc/mt8167/mt8167-power-regs.h>
#include <soc/mt8167/mt8167-power.h>
#include <threads.h>

namespace power {

// Describes a voltage regulator
class MtkRegulator {
public:
    MtkRegulator(ddk::MmioView pmic_mmio, const uint32_t default_voltage, const uint32_t enable_reg,
                 const uint8_t enable_bit)
        : default_voltage_(default_voltage), enable_register_(enable_reg), enable_bit_(enable_bit),
          cur_voltage_(default_voltage), pmic_mmio_(pmic_mmio) {}

    uint32_t enable_register() { return enable_register_; }
    uint8_t enable_bit() { return enable_bit_; }
    uint32_t cur_voltage() { return cur_voltage_; }
    uint32_t default_voltage() { return default_voltage_; }
    bool enabled() { return enabled_; }

    zx_status_t Enable();
    zx_status_t Disable();

private:
    const uint32_t default_voltage_;
    const uint32_t enable_register_;
    const uint8_t enable_bit_;
    uint32_t cur_voltage_;
    ddk::MmioView pmic_mmio_;
    bool enabled_ = false;

    void WaitForIdle();
    void WaitForValidClear();
    zx_status_t ReadPMICReg(uint32_t reg_addr, uint32_t* reg_value);
    zx_status_t WritePMICReg(uint32_t reg_addr, uint32_t value);
};

class MtkBuckRegulator : public MtkRegulator {
public:
    MtkBuckRegulator(ddk::MmioView pmic_mmio, const uint32_t enable_reg, const uint8_t enable_bit,
                     const uint32_t voltage_sel_reg, const uint32_t voltage_sel_mask,
                     const uint32_t buck_voltage_ctrl_reg, const uint32_t buck_voltage_on_reg,
                     const uint32_t min_voltage, const uint32_t max_voltage,
                     const uint32_t step_size)
        : MtkRegulator(pmic_mmio, min_voltage, enable_reg, enable_bit),
          buck_voltage_ctrl_reg_(buck_voltage_ctrl_reg), buck_voltage_on_reg_(buck_voltage_on_reg),
          voltage_sel_reg_(voltage_sel_reg), voltage_sel_mask_(voltage_sel_mask),
          min_voltage_(min_voltage), max_voltage_(max_voltage),
          step_size_(step_size) {}

    uint32_t buck_voltage_ctrl_reg() { return buck_voltage_ctrl_reg_; }
    uint32_t buck_voltage_on_reg() { return buck_voltage_on_reg_; }
    uint32_t voltage_sel_reg() { return voltage_sel_reg_; }
    uint32_t voltage_sel_mask() { return voltage_sel_mask_; }
    uint32_t min_voltage() { return min_voltage_; }
    uint32_t max_voltage() { return max_voltage_; }
    uint32_t step_size() { return step_size_; }
private:
    const uint32_t buck_voltage_ctrl_reg_;
    const uint32_t buck_voltage_on_reg_;
    const uint32_t voltage_sel_reg_;
    const uint32_t voltage_sel_mask_;
    const uint32_t min_voltage_;
    const uint32_t max_voltage_;
    const uint32_t step_size_;
};

class MtkLdoRegulator : public MtkRegulator {
public:
    MtkLdoRegulator(ddk::MmioView pmic_mmio, const uint32_t enable_reg, const uint8_t enable_bit,
                    const uint32_t voltage_sel_reg, const uint32_t voltage_sel_mask,
                    const fbl::Vector<uint32_t>& supported_voltages)
        : MtkRegulator(pmic_mmio, 0, enable_reg, enable_bit),
          voltage_sel_reg_(voltage_sel_reg), voltage_sel_mask_(voltage_sel_mask),
          supported_voltages_(supported_voltages) {}
    uint32_t voltage_sel_reg() { return voltage_sel_reg_; }
    uint32_t voltage_sel_mask() { return voltage_sel_mask_; }
    const fbl::Vector<uint32_t>& supported_voltages() { return supported_voltages_; }
private:
    const uint32_t voltage_sel_reg_;
    const uint32_t voltage_sel_mask_;
    const fbl::Vector<uint32_t>& supported_voltages_;
};

class MtkFixedRegulator : public MtkRegulator {
public:
    MtkFixedRegulator(ddk::MmioView pmic_mmio, const uint32_t default_voltage,
                      const uint32_t enable_reg, const uint8_t enable_bit)
        : MtkRegulator(pmic_mmio, default_voltage, enable_reg, enable_bit) {}
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
    void DdkUnbind();

    zx_status_t PowerImplGetPowerDomainStatus(uint32_t index, power_domain_status_t* out_status);
    zx_status_t PowerImplEnablePowerDomain(uint32_t index);
    zx_status_t PowerImplDisablePowerDomain(uint32_t index);
    zx_status_t PowerImplGetSupportedVoltageRange(uint32_t index, uint32_t* min_voltage,
                                                  uint32_t* max_voltage);
    zx_status_t PowerImplRequestVoltage(uint32_t index, uint32_t voltage, uint32_t* actual_voltage);
    zx_status_t PowerImplWritePmicCtrlReg(uint32_t index, uint32_t addr, uint32_t value);
    zx_status_t PowerImplReadPmicCtrlReg(uint32_t index, uint32_t addr, uint32_t* value);

protected:
    void InitializePowerDomains();
    std::array<std::optional<MtkRegulator>, kMt8167NumPowerDomains> power_domains_;

private:
    ddk::MmioBuffer pmic_mmio_;

    zx_status_t Bind();
    zx_status_t Init();
    void WaitForIdle();
    void WaitForValidClear();
    zx_status_t ReadPMICReg(uint32_t reg_addr, uint32_t* reg_value);
    zx_status_t WritePMICReg(uint32_t reg_addr, uint32_t value);
};

} // namespace power
