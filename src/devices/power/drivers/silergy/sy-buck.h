// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_SILERGY_SY_BUCK_H_
#define SRC_DEVICES_POWER_DRIVERS_SILERGY_SY_BUCK_H_

#include <ddktl/device.h>
#include <ddktl/protocol/i2c.h>
#include <ddktl/protocol/vreg.h>

#include "sy-buck-regs.h"

namespace silergy {

class SyBuck;
using SyBuckType = ddk::Device<SyBuck, ddk::UnbindableNew>;

class SyBuck : public SyBuckType, public ddk::VregProtocol<SyBuck, ddk::base_protocol> {
 public:
  SyBuck(zx_device_t* parent, ddk::I2cProtocolClient i2c)
      : SyBuckType(parent), i2c_(std::move(i2c)) {}
  virtual ~SyBuck() = default;
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  SyBuck(const SyBuck&) = delete;
  SyBuck(SyBuck&&) = delete;
  SyBuck& operator=(const SyBuck&) = delete;
  SyBuck& operator=(SyBuck&&) = delete;

  // Vreg Implementation.
  zx_status_t VregSetVoltageStep(uint32_t step);
  uint32_t VregGetVoltageStep();
  void VregGetRegulatorParams(vreg_params_t* out_params);

  // Device Protocol Implementation
  void DdkRelease() {}
  void DdkUnbindNew(ddk::UnbindTxn txn) {}

 protected:
  zx_status_t Init();

 protected:
  static constexpr uint32_t kMinVoltageUv = 600000;
  static constexpr uint32_t kVoltageStepUv = 12500;
  static constexpr uint32_t kNumSteps = 64;

 private:
  ddk::I2cProtocolClient i2c_;
  uint32_t current_step_;
  const Vsel vsel_ = Vsel::Vsel0;  // TODO(gkalsi): Get from Metadata.

};  // class SyBuck

}  // namespace silergy

#endif  // SRC_DEVICES_POWER_DRIVERS_SILERGY_SY_BUCK_H_
