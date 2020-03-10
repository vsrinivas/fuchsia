// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_POWER_POWER_H_
#define SRC_DEVICES_POWER_DRIVERS_POWER_POWER_H_

#include <memory>
#include <vector>

#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/power.h>
#include <ddktl/protocol/powerimpl.h>
#include <fbl/mutex.h>

namespace power {
class PowerDeviceComponentChild;
class PowerDevice;
using PowerDeviceType = ddk::Device<PowerDevice, ddk::UnbindableNew, ddk::Multibindable>;

class PowerDevice : public PowerDeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_POWER> {
 public:
  PowerDevice(zx_device_t* parent, ddk::PowerImplProtocolClient& power, uint32_t index)
      : PowerDeviceType(parent), power_(power), index_(index) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkUnbindNew(ddk::UnbindTxn txn);
  zx_status_t DdkOpenProtocolSessionMultibindable(uint32_t proto_id, void* ctx);
  zx_status_t DdkCloseProtocolSessionMultibindable(void* child_ctx);
  void DdkRelease();

  zx_status_t EnablePowerDomain(uint64_t component_device_id);
  zx_status_t DisablePowerDomain(uint64_t component_device_id);
  zx_status_t GetPowerDomainStatus(uint64_t component_device_id, power_domain_status_t* out_status);
  zx_status_t GetSupportedVoltageRange(uint64_t component_device_id, uint32_t* min_voltage,
                                       uint32_t* max_voltage);
  zx_status_t RequestVoltage(uint64_t component_device_id, uint32_t voltage,
                             uint32_t* actual_voltage);
  zx_status_t GetCurrentVoltage(uint64_t component_device_id, uint32_t index,
                                uint32_t* current_voltage);
  zx_status_t WritePmicCtrlReg(uint64_t component_device_id, uint32_t reg_addr, uint32_t value);
  zx_status_t ReadPmicCtrlReg(uint64_t component_device_id, uint32_t reg_addr, uint32_t* out_value);

 private:
  PowerDeviceComponentChild* GetComponentChild(uint64_t component_device_id);
  const ddk::PowerImplProtocolClient power_;
  const uint32_t index_;
  fbl::Mutex children_lock_;
  std::vector<std::unique_ptr<PowerDeviceComponentChild>> children_ __TA_GUARDED(children_lock_);
};

class PowerDeviceComponentChild
    : public ddk::PowerProtocol<PowerDeviceComponentChild, ddk::base_protocol> {
 public:
  explicit PowerDeviceComponentChild(uint64_t component_device_id, PowerDevice* parent)
      : component_device_id_(component_device_id), parent_(parent) {}

  zx_status_t PowerEnablePowerDomain();
  zx_status_t PowerDisablePowerDomain();
  zx_status_t PowerGetPowerDomainStatus(power_domain_status_t* out_status);
  zx_status_t PowerGetSupportedVoltageRange(uint32_t* min_voltage, uint32_t* max_voltage);
  zx_status_t PowerRequestVoltage(uint32_t voltage, uint32_t* actual_voltage);
  zx_status_t PowerGetCurrentVoltage(uint32_t index, uint32_t* current_voltage);
  zx_status_t PowerWritePmicCtrlReg(uint32_t reg_addr, uint32_t value);
  zx_status_t PowerReadPmicCtrlReg(uint32_t reg_addr, uint32_t* out_value);
  uint64_t component_device_id() const { return component_device_id_; }
  power_protocol_ops_t* ops() { return &power_protocol_ops_; }

 private:
  uint64_t component_device_id_;
  PowerDevice* parent_;
};

}  // namespace power

#endif  // SRC_DEVICES_POWER_DRIVERS_POWER_POWER_H_
