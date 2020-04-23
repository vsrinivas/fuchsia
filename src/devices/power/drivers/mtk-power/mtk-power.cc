// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtk-power.h"

#include <lib/device-protocol/pdev.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <soc/mt8167/mt8167-power-regs.h>
#include <soc/mt8167/mt8167-power.h>

namespace power {

const fbl::Vector<uint32_t> kSupportedVoltageList1{1800000, 1900000, 2000000, 2200000};
const fbl::Vector<uint32_t> kSupportedVoltageList2{3300000, 3400000, 3500000, 3600000};
const fbl::Vector<uint32_t> kSupportedVoltageList3{1800000, 3300000};
const fbl::Vector<uint32_t> kSupportedVoltageList4{3000000, 3300000};
const fbl::Vector<uint32_t> kSupportedVoltageList5{1200000, 1300000, 1500000, 1800000,
                                                   2000000, 2800000, 3000000, 3300000};
const fbl::Vector<uint32_t> kSupportedVoltageList6{1240000, 1390000};
const fbl::Vector<uint32_t> kSupportedVoltageList7{1200000, 1300000, 1500000, 1800000};
const fbl::Vector<uint32_t> kSupportedVoltageList8{1800000, 2000000};

void MtkRegulator::WaitForIdle() {
  while (PmicWacs2RData::Get().ReadFrom(&pmic_mmio_).wacs2_fsm() != PmicWacs2RData::kFsmStateIdle) {
  }
}

void MtkRegulator::WaitForValidClear() {
  while (PmicWacs2RData::Get().ReadFrom(&pmic_mmio_).wacs2_fsm() !=
         PmicWacs2RData::kFsmStateWfVldClear) {
  }
}

zx_status_t MtkRegulator::ReadPMICReg(uint32_t reg_addr, uint32_t* reg_value) {
  WaitForIdle();
  PmicWacs2Cmd::Get()
      .FromValue(0)
      .set_wacs2_write(0)
      .set_wacs2_addr(reg_addr >> 1)
      .WriteTo(&pmic_mmio_);
  // Wait for data to be available.
  WaitForValidClear();

  *reg_value = PmicWacs2RData::Get().ReadFrom(&pmic_mmio_).wacs2_rdata();

  // Data is read. clear the valid flag.
  PmicWacs2VldClear::Get().ReadFrom(&pmic_mmio_).set_wacs2_vldclr(1).WriteTo(&pmic_mmio_);
  return ZX_OK;
}

zx_status_t MtkRegulator::WritePMICReg(uint32_t reg_addr, uint32_t value) {
  WaitForIdle();
  PmicWacs2Cmd::Get()
      .FromValue(0)
      .set_wacs2_write(1)
      .set_wacs2_addr(reg_addr >> 1)
      .set_wacs2_data(value)
      .WriteTo(&pmic_mmio_);
  return ZX_OK;
}

zx_status_t MtkRegulator::Enable() {
  if (enabled_) {
    return ZX_OK;
  }
  uint32_t cur_val;
  zx_status_t status = ReadPMICReg(enable_register_, &cur_val);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Reading PMIC reg failed: %d", __FUNCTION__, status);
    return status;
  }
  status = WritePMICReg(enable_register_, (cur_val | 1 << enable_bit_));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Writing PMIC reg failed: %d", __FUNCTION__, status);
    return status;
  }

  enabled_ = true;
  return ZX_OK;
}

zx_status_t MtkRegulator::Disable() {
  if (!enabled_) {
    return ZX_ERR_BAD_STATE;
  }
  uint32_t cur_val;
  zx_status_t status = ReadPMICReg(enable_register_, &cur_val);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Reading PMIC reg failed: %d", __FUNCTION__, status);
    return status;
  }
  status = WritePMICReg(enable_register_, (cur_val &= ~(1 << enable_bit_)));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Writing PMIC reg failed: %d", __FUNCTION__, status);
    return status;
  }

  enabled_ = false;
  return ZX_OK;
}

zx_status_t MtkBuckRegulator::SetVoltageSelReg() {
  uint32_t ctrl_reg_val;
  zx_status_t status = ReadPMICReg(buck_voltage_ctrl_reg_, &ctrl_reg_val);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Reading PMIC reg failed: %d", __FUNCTION__, status);
    return status;
  }

  if (ctrl_reg_val & (1 << 1)) {
    voltage_sel_reg_ = buck_voltage_on_reg_;
  }
  return ZX_OK;
}

zx_status_t MtkBuckRegulator::GetVoltageSelector(uint32_t set_voltage, uint32_t* actual_voltage,
                                                 uint16_t* selector) {
  if (!step_size_) {
    return ZX_ERR_BAD_STATE;
  }
  if (set_voltage < min_voltage_) {
    zxlogf(ERROR, "%s Voltage :%x is not a supported voltage", __FUNCTION__, set_voltage);
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (set_voltage > max_voltage_) {
    zxlogf(ERROR, "%s Voltage :%x is not a supported voltage", __FUNCTION__, set_voltage);
    return ZX_ERR_NOT_SUPPORTED;
  }
  uint16_t sel = static_cast<uint16_t>((set_voltage - min_voltage_) / step_size_);
  *actual_voltage = min_voltage_ + (sel * step_size_);
  *selector = sel;
  return ZX_OK;
}

zx_status_t MtkBuckRegulator::RequestVoltage(uint32_t voltage, uint32_t* actual_voltage) {
  uint16_t selector = 0;
  zx_status_t status = GetVoltageSelector(voltage, actual_voltage, &selector);
  if (status != ZX_OK) {
    return status;
  }

  if (cur_voltage_ == *actual_voltage) {
    return ZX_OK;
  }
  uint32_t cur_val;
  status = ReadPMICReg(voltage_sel_reg_, &cur_val);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Reading PMIC reg failed: %d", __FUNCTION__, status);
    return status;
  }

  cur_val &= ~voltage_sel_mask_;
  cur_val |= (selector & voltage_sel_mask_);

  status = WritePMICReg(voltage_sel_reg_, cur_val);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Writing PMIC reg failed: %d", __FUNCTION__, status);
    return status;
  }

  cur_voltage_ = *actual_voltage;
  return ZX_OK;
}

zx_status_t MtkLdoRegulator::GetVoltageSelector(uint32_t set_voltage, uint32_t* actual_voltage,
                                                uint16_t* selector) {
  size_t num_voltages = supported_voltages_.size();
  if (num_voltages == 0) {
    return ZX_ERR_BAD_STATE;
  }

  if (set_voltage < supported_voltages_[0] || set_voltage > supported_voltages_[num_voltages - 1]) {
    zxlogf(ERROR, "%s Voltage :%x is not a supported voltage", __FUNCTION__, set_voltage);
    return ZX_ERR_NOT_SUPPORTED;
  }
  for (size_t i = 0; i < num_voltages; i++) {
    uint32_t voltage = supported_voltages_[i];
    if (voltage == set_voltage) {
      *selector = static_cast<uint16_t>(i);
      *actual_voltage = voltage;
      return ZX_OK;
    }
    if (set_voltage > voltage && set_voltage < supported_voltages_[i + 1]) {
      *selector = static_cast<uint16_t>(i);
      *actual_voltage = voltage;
      return ZX_OK;
    }
  }
  return ZX_ERR_BAD_STATE;
}

zx_status_t MtkLdoRegulator::RequestVoltage(uint32_t voltage, uint32_t* actual_voltage) {
  uint16_t selector = 0;
  zx_status_t status = GetVoltageSelector(voltage, actual_voltage, &selector);
  if (status != ZX_OK) {
    return status;
  }

  if (cur_voltage_ == *actual_voltage) {
    return ZX_OK;
  }
  uint32_t cur_val;
  status = ReadPMICReg(voltage_sel_reg_, &cur_val);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Reading PMIC reg failed: %d", __FUNCTION__, status);
    return status;
  }

  cur_val &= ~voltage_sel_mask_;
  cur_val |= ((selector << voltage_sel_shift_) & voltage_sel_mask_);

  status = WritePMICReg(voltage_sel_reg_, cur_val);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Writing PMIC reg failed: %d", __FUNCTION__, status);
    return status;
  }

  cur_voltage_ = *actual_voltage;
  return ZX_OK;
}

zx_status_t MtkPower::PowerImplGetCurrentVoltage(uint32_t index, uint32_t* current_voltage) {
  if (index >= kMt8167NumPowerDomains) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  *current_voltage = power_domains_[index]->cur_voltage();
  return ZX_OK;
}

zx_status_t MtkPower::PowerImplDisablePowerDomain(uint32_t index) {
  if (index >= kMt8167NumPowerDomains) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  zx_status_t status = power_domains_[index]->Disable();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Disable power domain %d failed. Status: %d", __FUNCTION__, index, status);
    return status;
  }

  return ZX_OK;
}

zx_status_t MtkPower::PowerImplEnablePowerDomain(uint32_t index) {
  if (index >= kMt8167NumPowerDomains) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return power_domains_[index]->Enable();
}

zx_status_t MtkPower::PowerImplGetPowerDomainStatus(uint32_t index,
                                                    power_domain_status_t* out_status) {
  if (index >= kMt8167NumPowerDomains) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  *out_status =
      power_domains_[index]->enabled() ? POWER_DOMAIN_STATUS_ENABLED : POWER_DOMAIN_STATUS_DISABLED;
  return ZX_OK;
}

zx_status_t MtkPower::PowerImplGetSupportedVoltageRange(uint32_t index, uint32_t* min_voltage,
                                                        uint32_t* max_voltage) {
  if (index >= kMt8167NumPowerDomains) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return power_domains_[index]->GetSupportedVoltageRange(min_voltage, max_voltage);
}

zx_status_t MtkPower::PowerImplRequestVoltage(uint32_t index, uint32_t voltage,
                                              uint32_t* actual_voltage) {
  if (index >= kMt8167NumPowerDomains) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return power_domains_[index]->RequestVoltage(voltage, actual_voltage);
}

zx_status_t MtkPower::PowerImplWritePmicCtrlReg(uint32_t index, uint32_t reg_addr, uint32_t value) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkPower::PowerImplReadPmicCtrlReg(uint32_t index, uint32_t addr, uint32_t* value) {
  return ZX_ERR_NOT_SUPPORTED;
}

void MtkPower::DdkRelease() { delete this; }

void MtkPower::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

enum MtkRegulatorType { BUCK = 1, LDO, FIXED };

struct MtkRegulatorParams {
  uint8_t type;
  uint32_t enable_register = 0;
  uint8_t enable_bit = 0;
  uint32_t select_register = 0;
  uint32_t select_mask = 0;
  uint32_t select_shift = 0;
  uint32_t buck_voltage_control_register = 0;
  uint32_t buck_voltage_on_register = 0;
  uint32_t min_voltage = 0;
  uint32_t max_voltage = 0;
  uint32_t default_voltage = 0;
  uint32_t step_size = 0;
  const fbl::Vector<uint32_t>& supported_voltage = kSupportedVoltageList1;
};

MtkRegulatorParams kMtkRegulatorParams[] = {
    [kBuckVProc] = {.type = BUCK,
                    .enable_register = kPmicVprocCon7,
                    .enable_bit = 1,
                    .select_register = kPmicVprocCon9,
                    .select_mask = 0x7f,
                    .select_shift = 0,
                    .buck_voltage_control_register = kPmicVprocCon5,
                    .buck_voltage_on_register = kPmicVprocCon10,
                    .min_voltage = 700000,
                    .max_voltage = 1493750,
                    .step_size = 6250},
    [kBuckVCore] = {.type = BUCK,
                    .enable_register = kPmicVcoreCon7,
                    .enable_bit = 1,
                    .select_register = kPmicVcoreCon9,
                    .select_mask = 0x7f,
                    .select_shift = 0,
                    .buck_voltage_control_register = kPmicVcoreCon5,
                    .buck_voltage_on_register = kPmicVcoreCon10,
                    .min_voltage = 700000,
                    .max_voltage = 1493750,
                    .step_size = 6250},
    [kBuckVSys] = {.type = BUCK,
                   .enable_register = kPmicVsysCon7,
                   .enable_bit = 1,
                   .select_register = kPmicVsysCon9,
                   .select_mask = 0x7f,
                   .select_shift = 0,
                   .buck_voltage_control_register = kPmicVsysCon5,
                   .buck_voltage_on_register = kPmicVsysCon10,
                   .min_voltage = 1400000,
                   .max_voltage = 2987500,
                   .step_size = 12500},
    [kALdoVAud28] =
        {
            .type = FIXED,
            .enable_register = kPmicAnaLdoCon23,
            .enable_bit = 14,
            .default_voltage = 2800000,
        },
    [kALdoVAud22] =
        {
            .type = LDO,
            .enable_register = kPmicAnaLdoCon2,
            .enable_bit = 14,
            .select_register = kPmicAnaLdoCon8,
            .select_mask = 0x60,
            .select_shift = 5,
            .supported_voltage = kSupportedVoltageList1,
        },
    [kALdoVAdc18] =
        {
            .type = FIXED,
            .enable_register = kPmicAnaLdoCon25,
            .enable_bit = 14,
            .default_voltage = 1800000,
        },
    [kALdoVXo22] =
        {
            .type = FIXED,
            .enable_register = kPmicAnaLdoCon1,
            .enable_bit = 10,
            .default_voltage = 2800000,
        },
    [kALdoVCamA] =
        {
            .type = FIXED,
            .enable_register = kPmicAnaLdoCon4,
            .enable_bit = 15,
            .default_voltage = 2800000,
        },
    [kVSysLdoVm] =
        {
            .type = LDO,
            .enable_register = kPmicDigLdoCon47,
            .enable_bit = 14,
            .select_register = kPmicDigLdoCon48,
            .select_mask = 0x30,
            .select_shift = 4,
            .supported_voltage = kSupportedVoltageList6,
        },
    [kVSysLdoVcn18] =
        {
            .type = FIXED,
            .enable_register = kPmicDigLdoCon11,
            .enable_bit = 14,
            .default_voltage = 1800000,
        },
    [kVSysLdoVio18] =
        {
            .type = FIXED,
            .enable_register = kPmicDigLdoCon49,
            .enable_bit = 14,
            .default_voltage = 1800000,
        },
    [kVSysLdoVCamIo] =
        {
            .type = FIXED,
            .enable_register = kPmicDigLdoCon53,
            .enable_bit = 14,
            .default_voltage = 1800000,
        },
    [kVSysLdoVCamD] =
        {
            .type = LDO,
            .enable_register = kPmicDigLdoCon51,
            .enable_bit = 14,
            .select_register = kPmicDigLdoCon52,
            .select_mask = 0x60,
            .select_shift = 5,
            .supported_voltage = kSupportedVoltageList7,
        },
    [kVDLdoVcn35] = {.type = LDO,
                     .enable_register = kPmicAnaLdoCon21,
                     .enable_bit = 12,
                     .select_register = kPmicAnaLdoCon16,
                     .select_mask = 0xC,
                     .select_shift = 6,
                     .supported_voltage = kSupportedVoltageList2},
    [kVDLdoVio28] =
        {
            .type = FIXED,
            .enable_register = kPmicDigLdoCon0,
            .enable_bit = 14,
            .default_voltage = 2800000,
        },
    [kVDLdoVemc33] = {.type = LDO,
                      .enable_register = kPmicDigLdoCon6,
                      .enable_bit = 14,
                      .select_register = kPmicDigLdoCon27,
                      .select_mask = 0x80,
                      .select_shift = 7,
                      .supported_voltage = kSupportedVoltageList4},
    [kVDLdoVmc] = {.type = LDO,
                   .enable_register = kPmicDigLdoCon3,
                   .enable_bit = 12,
                   .select_register = kPmicDigLdoCon24,
                   .select_mask = 0x10,
                   .select_shift = 4,
                   .supported_voltage = kSupportedVoltageList3},
    [kVDLdoVmch] = {.type = LDO,
                    .enable_register = kPmicDigLdoCon5,
                    .enable_bit = 14,
                    .select_register = kPmicDigLdoCon26,
                    .select_mask = 0x80,
                    .select_shift = 7,
                    .supported_voltage = kSupportedVoltageList4},
    [kVDLdoVUsb33] =
        {
            .type = FIXED,
            .enable_register = kPmicDigLdoCon2,
            .enable_bit = 14,
            .default_voltage = 3300000,
        },
    [kVDLdoVGp1] = {.type = LDO,
                    .enable_register = kPmicDigLdoCon7,
                    .enable_bit = 15,
                    .select_register = kPmicDigLdoCon28,
                    .select_mask = 0xE0,
                    .select_shift = 5,
                    .supported_voltage = kSupportedVoltageList5},
    [kVDLdoVM25] =
        {
            .type = FIXED,
            .enable_register = kPmicDigLdoCon55,
            .enable_bit = 14,
            .default_voltage = 2500000,
        },
    [kVDLdoVGp2] = {.type = LDO,
                    .enable_register = kPmicDigLdoCon8,
                    .enable_bit = 15,
                    .select_register = kPmicDigLdoCon29,
                    .select_mask = 0xE0,
                    .select_shift = 5,
                    .supported_voltage = kSupportedVoltageList5},
    [kVDLdoVCamAf] = {.type = LDO,
                      .enable_register = kPmicDigLdoCon31,
                      .enable_bit = 15,
                      .select_register = kPmicDigLdoCon32,
                      .select_mask = 0xE0,
                      .select_shift = 5,
                      .supported_voltage = kSupportedVoltageList5},
};

void MtkPower::InitializePowerDomains() {
  for (size_t i = 0; i < kMt8167NumPowerDomains; i++) {
    auto& reg_params = kMtkRegulatorParams[i];
    if (reg_params.type == BUCK) {
      power_domains_[i] = std::make_unique<MtkBuckRegulator>(
          pmic_mmio_.View(0), reg_params.enable_register, reg_params.enable_bit,
          reg_params.select_register, reg_params.select_mask, reg_params.select_shift,
          reg_params.buck_voltage_control_register, reg_params.buck_voltage_on_register,
          reg_params.min_voltage, reg_params.max_voltage, reg_params.step_size);
      MtkBuckRegulator* buck = static_cast<MtkBuckRegulator*>(power_domains_[i].get());
      buck->SetVoltageSelReg();
    } else if (reg_params.type == FIXED) {
      power_domains_[i] =
          std::make_unique<MtkFixedRegulator>(pmic_mmio_.View(0), reg_params.default_voltage,
                                              reg_params.enable_register, reg_params.enable_bit);
    } else if (reg_params.type == LDO) {
      power_domains_[i] = std::make_unique<MtkLdoRegulator>(
          pmic_mmio_.View(0), reg_params.enable_register, reg_params.enable_bit,
          reg_params.select_register, reg_params.select_mask, reg_params.select_shift,
          reg_params.supported_voltage);
    }
  }
}

zx_status_t MtkPower::Init() {
  // TODO(ravoorir): Check if bootloader did not init the PMIC and
  // do the needful.
  InitializePowerDomains();
  return ZX_OK;
}

zx_status_t MtkPower::Bind() {
  pbus_protocol_t pbus;
  zx_status_t status = device_get_protocol(parent(), ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to get ZX_PROTOCOL_PBUS, %d", __FUNCTION__, status);
    return status;
  }

  power_impl_protocol_t power_proto = {
      .ops = &power_impl_protocol_ops_,
      .ctx = this,
  };

  status = pbus_register_protocol(&pbus, ZX_PROTOCOL_POWER_IMPL, &power_proto, sizeof(power_proto));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s pbus_register_protocol failed: %d", __FUNCTION__, status);
    return status;
  }
  status = DdkAdd("mtk-power", DEVICE_ADD_ALLOW_MULTI_COMPOSITE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s DdkAdd failed: %d", __FUNCTION__, status);
  }

  return status;
}

zx_status_t MtkPower::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status = ZX_OK;

  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s Could not get pdev: %d", __FUNCTION__, status);
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> mmio;
  status = pdev.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Failed to get mmio: %d", __FUNCTION__, status);
    return status;
  }
  auto dev = std::make_unique<MtkPower>(parent, *std::move(mmio));

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

static constexpr zx_driver_ops_t mtk_power_driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = MtkPower::Create;
  return driver_ops;
}();

}  // namespace power

ZIRCON_DRIVER_BEGIN(mtk_power, power::mtk_power_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_POWER), ZIRCON_DRIVER_END(mtk_power)
