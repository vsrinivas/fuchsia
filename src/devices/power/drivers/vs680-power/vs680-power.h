// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_VS680_POWER_VS680_POWER_H_
#define SRC_DEVICES_POWER_DRIVERS_VS680_POWER_VS680_POWER_H_

#include <lib/zircon-internal/thread_annotations.h>

#include <ddktl/device.h>
#include <ddktl/protocol/i2c.h>
#include <ddktl/protocol/powerimpl.h>
#include <fbl/mutex.h>

namespace power {

class Vs680Power : public ddk::Device<Vs680Power>,
                   public ddk::PowerImplProtocol<Vs680Power, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  Vs680Power(zx_device_t* parent, const ddk::I2cProtocolClient& pmic_i2c)
      : ddk::Device<Vs680Power>(parent), pmic_i2c_(pmic_i2c) {}
  ~Vs680Power() {}

  void DdkRelease() { delete this; }

  zx_status_t PowerImplGetPowerDomainStatus(uint32_t index, power_domain_status_t* out_status);
  zx_status_t PowerImplEnablePowerDomain(uint32_t index);
  zx_status_t PowerImplDisablePowerDomain(uint32_t index);
  zx_status_t PowerImplGetSupportedVoltageRange(uint32_t index, uint32_t* out_min,
                                                uint32_t* out_max);
  zx_status_t PowerImplRequestVoltage(uint32_t index, uint32_t voltage,
                                      uint32_t* out_actual_voltage);
  zx_status_t PowerImplGetCurrentVoltage(uint32_t index, uint32_t* out_current_voltage);
  zx_status_t PowerImplWritePmicCtrlReg(uint32_t index, uint32_t reg_addr, uint32_t value);
  zx_status_t PowerImplReadPmicCtrlReg(uint32_t index, uint32_t reg_addr, uint32_t* out_value);

 private:
  fbl::Mutex lock_;
  ddk::I2cProtocolClient pmic_i2c_ TA_GUARDED(lock_);
};

}  // namespace power

#endif  // SRC_DEVICES_POWER_DRIVERS_VS680_POWER_VS680_POWER_H_
