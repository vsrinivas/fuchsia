// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_MSM8X53_POWER_MSM8X53_POWER_H_
#define SRC_DEVICES_POWER_DRIVERS_MSM8X53_POWER_MSM8X53_POWER_H_

#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/powerimpl.h>
#include <soc/msm8x53/msm8x53-power-regs.h>

namespace power {

enum Msm8x53PowerDomainType {
  RPM_REGULATOR,
  SPM_REGULATOR,
  PMIC_CTRL_REGISTER,
};

// TODO(ravoorir): Add data types for each data structure.
struct Msm8x53PowerDomainInfo {
  Msm8x53PowerDomainType type;
};

class Msm8x53Power;
using Msm8x53PowerType = ddk::Device<Msm8x53Power, ddk::Unbindable>;

class Msm8x53Power : public Msm8x53PowerType,
                     public ddk::PowerImplProtocol<Msm8x53Power, ddk::base_protocol> {
 public:
  explicit Msm8x53Power(zx_device_t* parent, ddk::MmioBuffer core_mmio, ddk::MmioBuffer chnls_mmio,
                        ddk::MmioBuffer obsvr_mmio, ddk::MmioBuffer intr_mmio,
                        ddk::MmioBuffer cfg_mmio)
      : Msm8x53PowerType(parent),
        core_mmio_(std::move(core_mmio)),
        chnls_mmio_(std::move(chnls_mmio)),
        obsvr_mmio_(std::move(obsvr_mmio)),
        intr_mmio_(std::move(intr_mmio)),
        cfg_mmio_(std::move(cfg_mmio)) {}

  ~Msm8x53Power() = default;
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
  zx_status_t PmicArbInit();
  uint32_t ppid_to_apid_[kMaxPPIDEntries];

 private:
  ddk::MmioBuffer core_mmio_;
  ddk::MmioBuffer chnls_mmio_;
  ddk::MmioBuffer obsvr_mmio_;
  ddk::MmioBuffer intr_mmio_;
  ddk::MmioBuffer cfg_mmio_;

  zx_status_t Init();
  zx_status_t ReadPMICReg(uint32_t reg_addr, uint32_t* reg_value);
  zx_status_t WritePMICReg(uint32_t reg_addr, uint32_t value);
  zx_status_t RpmRegulatorEnable(const Msm8x53PowerDomainInfo* domain);
  zx_status_t RpmRegulatorDisable(const Msm8x53PowerDomainInfo* domain);
  zx_status_t SpmRegulatorEnable(const Msm8x53PowerDomainInfo* domain);
  zx_status_t SpmRegulatorDisable(const Msm8x53PowerDomainInfo* domain);
};

}  // namespace power

#endif  // SRC_DEVICES_POWER_DRIVERS_MSM8X53_POWER_MSM8X53_POWER_H_
