// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/power.h>
#include <ddktl/protocol/powerimpl.h>

namespace power {

class PowerDevice;
using PowerDeviceType = ddk::Device<PowerDevice, ddk::UnbindableNew>;

class PowerDevice : public PowerDeviceType,
                    public ddk::PowerProtocol<PowerDevice, ddk::base_protocol> {
 public:
  PowerDevice(zx_device_t* parent, ddk::PowerImplProtocolClient& power, uint32_t index)
      : PowerDeviceType(parent), power_(power), index_(index) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t PowerEnablePowerDomain();
  zx_status_t PowerDisablePowerDomain();
  zx_status_t PowerGetPowerDomainStatus(power_domain_status_t* out_status);
  zx_status_t PowerGetSupportedVoltageRange(uint32_t* min_voltage, uint32_t* max_voltage);
  zx_status_t PowerRequestVoltage(uint32_t voltage, uint32_t* actual_voltage);
  zx_status_t PowerGetCurrentVoltage(uint32_t index, uint32_t* current_voltage);
  zx_status_t PowerWritePmicCtrlReg(uint32_t reg_addr, uint32_t value);
  zx_status_t PowerReadPmicCtrlReg(uint32_t reg_addr, uint32_t* out_value);

 private:
  const ddk::PowerImplProtocolClient power_;
  const uint32_t index_;
};

}  // namespace power
