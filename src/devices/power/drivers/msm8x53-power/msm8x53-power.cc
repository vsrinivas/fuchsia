// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msm8x53-power.h"

#include <lib/device-protocol/pdev.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <soc/msm8x53/msm8x53-power-regs.h>
#include <soc/msm8x53/msm8x53-power.h>

namespace power {

constexpr Msm8x53PowerDomainInfo kMsm8x53PowerDomains[] = {
    [kVRegS1] = {.type = RPM_REGULATOR},           [kVRegS2] = {.type = RPM_REGULATOR},
    [kVRegS3] = {.type = RPM_REGULATOR},           [kVRegS4] = {.type = RPM_REGULATOR},
    [kVRegS5] = {.type = RPM_REGULATOR},           [kVRegS6] = {.type = RPM_REGULATOR},
    [kVRegS7] = {.type = RPM_REGULATOR},           [kVRegLdoA1] = {.type = RPM_REGULATOR},
    [kVRegLdoA2] = {.type = RPM_REGULATOR},        [kVRegLdoA3] = {.type = RPM_REGULATOR},
    [kVRegLdoA5] = {.type = RPM_REGULATOR},        [kVRegLdoA6] = {.type = RPM_REGULATOR},
    [kVRegLdoA7] = {.type = RPM_REGULATOR},        [kVRegLdoA8] = {.type = RPM_REGULATOR},
    [kVRegLdoA9] = {.type = RPM_REGULATOR},        [kVRegLdoA10] = {.type = RPM_REGULATOR},
    [kVRegLdoA11] = {.type = RPM_REGULATOR},       [kVRegLdoA12] = {.type = RPM_REGULATOR},
    [kVRegLdoA13] = {.type = RPM_REGULATOR},       [kVRegLdoA16] = {.type = RPM_REGULATOR},
    [kVRegLdoA17] = {.type = RPM_REGULATOR},       [kVRegLdoA19] = {.type = RPM_REGULATOR},
    [kVRegLdoA22] = {.type = RPM_REGULATOR},       [kVRegLdoA23] = {.type = RPM_REGULATOR},
    [kPmicCtrlReg] = {.type = PMIC_CTRL_REGISTER},
};

zx_status_t Msm8x53Power::ReadPMICReg(uint32_t reg_addr, uint32_t* reg_value) {
  // Extract slave id, periph id and register offset.
  auto reg = PmicRegAddr::Get().FromValue(reg_addr);
  uint32_t reg_offset = reg.reg_offset();
  uint32_t periph_id = reg.periph_id();
  uint32_t slave_id = reg.slave_id();

  uint32_t ppid = PPID(slave_id, periph_id);
  // SID is 4 bits and PPID is 8 bits and ppid will always < 4096(kMaxPPIDEntries)
  // So there is not need for bounds check.
  ZX_DEBUG_ASSERT(ppid < kMaxPPIDEntries);

  uint32_t apid = ppid_to_apid_[ppid];
  // Disable Irq mode for the current channel.
  // TODO(ravoorir): Support Interrupts.
  uint32_t cmd_cfg_offset = PMIC_ARB_CHANNEL_CMD_CONFIG_OFFSET(apid);
  PmicArbCoreChannelCmdConfig::Get(cmd_cfg_offset)
      .ReadFrom(&obsvr_mmio_)
      .set_intr(0)
      .WriteTo(&obsvr_mmio_);
  // Write the CMD to read data
  // TODO(ravoorir): Update byte_cnt with actual number of bytes
  uint32_t cmd_offset = PMIC_ARB_CHANNEL_CMD_OFFSET(apid);
  PmicArbCoreChannelCmdInfo::Get(cmd_offset)
      .ReadFrom(&obsvr_mmio_)
      .set_byte_cnt(0)
      .set_reg_offset_addr(reg_offset)
      .set_periph_id(periph_id)
      .set_slave_id(slave_id)
      .set_priority(0)
      .set_opcode(kSpmiCmdRegReadOpcode)
      .WriteTo(&obsvr_mmio_);
  // Wait for CMD Completion
  uint32_t status = 0;
  while (!status) {
    status = PmicArbCoreChannelCmdStatus::Get(PMIC_ARB_CHANNEL_CMD_STATUS_OFFSET(apid))
                 .ReadFrom(&obsvr_mmio_)
                 .status();
  }
  if (status ^ PmicArbCoreChannelCmdStatus::kPmicArbCmdDone) {
    // Cmd completed with an error
    zxlogf(ERROR, "%s Unable to read Pmic Reg: 0x%x status: 0x%x\n", __FUNCTION__, reg_addr,
           status);
    return ZX_ERR_IO;
  }

  // Read RDATA0
  uint32_t rdata = PmicArbCoreChannelCmdRData::Get(PMIC_ARB_CHANNEL_CMD_RDATA0_OFFSET(apid))
                       .ReadFrom(&obsvr_mmio_)
                       .data();
  *reg_value = rdata;
  return ZX_OK;
}

zx_status_t Msm8x53Power::WritePMICReg(uint32_t reg_addr, uint32_t value) {
  // Extract slave id, periph id and register offset.
  auto reg = PmicRegAddr::Get().FromValue(reg_addr);
  uint32_t reg_offset = reg.reg_offset();
  uint32_t periph_id = reg.periph_id();
  uint32_t slave_id = reg.slave_id();

  uint32_t ppid = PPID(slave_id, periph_id);

  // SID is 4 bits and PPID is 8 bits and ppid will always < 4096(kMaxPPIDEntries)
  // So there is not need for bounds check.
  ZX_DEBUG_ASSERT(ppid < kMaxPPIDEntries);

  uint32_t apid = ppid_to_apid_[ppid];

  // Disable Irq mode for the current channel.
  // TODO(ravoorir): Support Interrupts later
  uint32_t cmd_cfg_offset = PMIC_ARB_CHANNEL_CMD_CONFIG_OFFSET(apid);
  PmicArbCoreChannelCmdConfig::Get(cmd_cfg_offset)
      .ReadFrom(&chnls_mmio_)
      .set_intr(0)
      .WriteTo(&chnls_mmio_);

  // Write first 4 bytes to WDATA0
  // TODO(ravoorir): Support writing of 8 byte data.
  PmicArbCoreChannelCmdWData::Get(PMIC_ARB_CHANNEL_CMD_WDATA0_OFFSET(apid))
      .ReadFrom(&chnls_mmio_)
      .set_data(value)
      .WriteTo(&chnls_mmio_);

  // Write the CMD
  // TODO(ravoorir): update byte_cnt to the write byte count.
  uint32_t cmd_offset = PMIC_ARB_CHANNEL_CMD_OFFSET(apid);
  PmicArbCoreChannelCmdInfo::Get(cmd_offset)
      .ReadFrom(&chnls_mmio_)
      .set_byte_cnt(0)
      .set_reg_offset_addr(reg_offset)
      .set_periph_id(periph_id)
      .set_slave_id(slave_id)
      .set_priority(0)
      .set_opcode(kSpmiCmdRegWriteOpcode)
      .WriteTo(&chnls_mmio_);

  // Wait for CMD Completion
  uint32_t status = 0;
  while (!status) {
    status = PmicArbCoreChannelCmdStatus::Get(PMIC_ARB_CHANNEL_CMD_STATUS_OFFSET(apid))
                 .ReadFrom(&chnls_mmio_)
                 .status();
  }
  if (status ^ PmicArbCoreChannelCmdStatus::kPmicArbCmdDone) {
    // Cmd completed with an error
    zxlogf(ERROR, "%s Unable to write PMIC Reg 0x%x status:0x%x\n", __FUNCTION__, reg_addr, status);
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t Msm8x53Power::RpmRegulatorEnable(const Msm8x53PowerDomainInfo* domain) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Power::RpmRegulatorDisable(const Msm8x53PowerDomainInfo* domain) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Power::SpmRegulatorEnable(const Msm8x53PowerDomainInfo* domain) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Power::SpmRegulatorDisable(const Msm8x53PowerDomainInfo* domain) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Power::PowerImplWritePmicCtrlReg(uint32_t index, uint32_t addr, uint32_t value) {
  if (index != kPmicCtrlReg) {
    return ZX_ERR_INVALID_ARGS;
  }
  return WritePMICReg(addr, value);
}

zx_status_t Msm8x53Power::PowerImplReadPmicCtrlReg(uint32_t index, uint32_t addr, uint32_t* value) {
  if (index != kPmicCtrlReg) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ReadPMICReg(addr, value);
}

zx_status_t Msm8x53Power::PowerImplDisablePowerDomain(uint32_t index) {
  if (index >= fbl::count_of(kMsm8x53PowerDomains)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  const Msm8x53PowerDomainInfo* domain = &kMsm8x53PowerDomains[index];
  if (domain->type == RPM_REGULATOR) {
    return RpmRegulatorDisable(domain);
  } else if (domain->type == SPM_REGULATOR) {
    return SpmRegulatorDisable(domain);
  }
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t Msm8x53Power::PowerImplEnablePowerDomain(uint32_t index) {
  if (index >= fbl::count_of(kMsm8x53PowerDomains)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const Msm8x53PowerDomainInfo* domain = &kMsm8x53PowerDomains[index];
  if (domain->type == RPM_REGULATOR) {
    return RpmRegulatorEnable(domain);
  } else if (domain->type == SPM_REGULATOR) {
    return SpmRegulatorEnable(domain);
  }
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t Msm8x53Power::PowerImplGetPowerDomainStatus(uint32_t index,
                                                        power_domain_status_t* out_status) {
  if (index >= fbl::count_of(kMsm8x53PowerDomains)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Power::PowerImplGetSupportedVoltageRange(uint32_t index, uint32_t* min_voltage,
                                                            uint32_t* max_voltage) {
  if (index >= fbl::count_of(kMsm8x53PowerDomains)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Power::PowerImplRequestVoltage(uint32_t index, uint32_t voltage,
                                                  uint32_t* actual_voltage) {
  if (index >= fbl::count_of(kMsm8x53PowerDomains)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Power::PowerImplGetCurrentVoltage(uint32_t index, uint32_t* current_voltage) {
  if (index >= fbl::count_of(kMsm8x53PowerDomains)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

void Msm8x53Power::DdkRelease() { delete this; }

void Msm8x53Power::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

zx_status_t Msm8x53Power::PmicArbInit() {
  // Read version
  static uint32_t pmic_arb_ver = PmicArbVersion::Get().ReadFrom(&core_mmio_).arb_version();
  zxlogf(ERROR, "%s Pmic Arbiter version: 0x%x\n", __FUNCTION__, pmic_arb_ver);
  if (pmic_arb_ver != kPmicArbVersionTwo) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  uint32_t slave_id = 0;
  uint32_t periph_id = 0;

  // Maintain PPID->APID mapping
  for (uint32_t apid = 0; apid < kMaxPmicPeripherals; apid++) {
    uint32_t core_channel_offset = PMIC_ARB_CORE_CHANNEL_INFO_OFFSET(apid);
    auto reg = PmicArbCoreChannelInfo::Get(core_channel_offset).ReadFrom(&core_mmio_);
    slave_id = reg.slave_id();
    periph_id = reg.periph_id();
    ppid_to_apid_[PPID(slave_id, periph_id)] = apid;
  }
  return ZX_OK;
}

zx_status_t Msm8x53Power::Init() {
  PmicArbInit();
  return ZX_OK;
}

zx_status_t Msm8x53Power::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status = ZX_OK;

  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s Could not get pdev: %d\n", __FUNCTION__, status);
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> core_mmio;
  status = pdev.MapMmio(kPmicArbCoreMmioIndex, &core_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Failed to get core mmio: %d\n", __FUNCTION__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> chnls_mmio;
  status = pdev.MapMmio(kPmicArbChnlsMmioIndex, &chnls_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Failed to get core mmio: %d\n", __FUNCTION__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> obsvr_mmio;
  status = pdev.MapMmio(kPmicArbObsrvrMmioIndex, &obsvr_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Failed to get core mmio: %d\n", __FUNCTION__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> intr_mmio;
  status = pdev.MapMmio(kPmicArbIntrMmioIndex, &intr_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Failed to get core mmio: %d\n", __FUNCTION__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> cfg_mmio;
  status = pdev.MapMmio(kPmicArbCnfgMmioIndex, &cfg_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Failed to get core mmio: %d\n", __FUNCTION__, status);
    return status;
  }

  auto dev = std::make_unique<Msm8x53Power>(parent, *std::move(core_mmio), *std::move(chnls_mmio),
                                            *std::move(obsvr_mmio), *std::move(intr_mmio),
                                            *std::move(cfg_mmio));

  if ((status = dev->Init()) != ZX_OK) {
    return status;
  }

  if ((status = dev->DdkAdd("msm8x53-power")) != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();
  return ZX_OK;
}

static constexpr zx_driver_ops_t msm8x53_power_driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = Msm8x53Power::Create;
  return driver_ops;
}();

}  // namespace power

ZIRCON_DRIVER_BEGIN(msm8x53_power, power::msm8x53_power_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_QUALCOMM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_QUALCOMM_POWER),
    ZIRCON_DRIVER_END(msm8x53_power)
