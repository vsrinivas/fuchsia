// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_POWER_POWER_H_
#define SRC_DEVICES_POWER_DRIVERS_POWER_POWER_H_

#include <fuchsia/hardware/power/cpp/banjo.h>
#include <fuchsia/hardware/powerimpl/cpp/banjo.h>

#include <memory>
#include <vector>

#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/mutex.h>

namespace power {
class PowerDeviceComponentChild;
class PowerDevice;
using PowerDeviceType = ddk::Device<PowerDevice, ddk::Unbindable, ddk::Multibindable>;

// Each power domain is modelled to be a power device and the power device class talks to
// a driver that implements ZX_PROTOCOL_POWER_IMPL, passing in the index of this power domain.
// For each dependent composite device of a PowerDevice(power domain), a PowerDeviceComponentChild
// is created.
class PowerDevice : public PowerDeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_POWER> {
 public:
  PowerDevice(zx_device_t* parent, uint32_t index, const ddk::PowerImplProtocolClient& power_impl,
              const ddk::PowerProtocolClient& parent_power, uint32_t min_voltage,
              uint32_t max_voltage, bool fixed)
      : PowerDeviceType(parent),
        index_(index),
        power_impl_(power_impl),
        parent_power_(parent_power),
        min_voltage_uV_(min_voltage),
        max_voltage_uV_(max_voltage),
        fixed_(fixed) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkUnbind(ddk::UnbindTxn txn);
  zx_status_t DdkOpenProtocolSessionMultibindable(uint32_t proto_id, void* ctx);
  zx_status_t DdkCloseProtocolSessionMultibindable(void* child_ctx);
  void DdkRelease();

  zx_status_t RegisterPowerDomain(uint64_t component_device_id, uint32_t min_needed_voltage_uV,
                                  uint32_t max_supported_voltage_uV);
  zx_status_t UnregisterPowerDomain(uint64_t component_device_id);
  zx_status_t GetPowerDomainStatus(uint64_t component_device_id, power_domain_status_t* out_status);
  zx_status_t GetSupportedVoltageRange(uint64_t component_device_id, uint32_t* min_voltage,
                                       uint32_t* max_voltage);
  zx_status_t RequestVoltage(uint64_t component_device_id, uint32_t voltage,
                             uint32_t* actual_voltage);
  zx_status_t GetCurrentVoltage(uint64_t component_device_id, uint32_t index,
                                uint32_t* current_voltage);
  zx_status_t WritePmicCtrlReg(uint64_t component_device_id, uint32_t reg_addr, uint32_t value);
  zx_status_t ReadPmicCtrlReg(uint64_t component_device_id, uint32_t reg_addr, uint32_t* out_value);
  uint32_t GetDependentCount();

 private:
  PowerDeviceComponentChild* GetComponentChildLocked(uint64_t component_device_id)
      __TA_REQUIRES(power_device_lock_);
  zx_status_t GetSuitableVoltageLocked(uint32_t voltage, uint32_t* suitable_voltage)
      __TA_REQUIRES(power_device_lock_);
  uint32_t GetDependentCountLocked() __TA_REQUIRES(power_device_lock_);
  const uint32_t index_;
  const ddk::PowerImplProtocolClient power_impl_;
  const ddk::PowerProtocolClient parent_power_;
  fbl::Mutex power_device_lock_;
  std::vector<std::unique_ptr<PowerDeviceComponentChild>> children_;
  // Min supported voltage of this domain
  uint32_t min_voltage_uV_;
  // Max supported voltage of this domain
  uint32_t max_voltage_uV_;
  // Does it support voltage modifications?
  bool fixed_;
};

// For each composite device that is dependent on a PowerDevice(power domain),
// an object of this class is created. This class maintains the context that is specific
// to the composite device. All the power protocol ops made by the composite device first
// arrive on this calss and are forwarded to the PowerDevice with the corresponding composite
// device context(component_device_id).
class PowerDeviceComponentChild
    : public ddk::PowerProtocol<PowerDeviceComponentChild, ddk::base_protocol> {
 public:
  explicit PowerDeviceComponentChild(uint64_t component_device_id, PowerDevice* parent)
      : component_device_id_(component_device_id), power_device_(parent) {}

  zx_status_t PowerRegisterPowerDomain(uint32_t min_needed_voltage_uV,
                                       uint32_t max_supported_voltage_uV);
  zx_status_t PowerUnregisterPowerDomain();
  zx_status_t PowerGetPowerDomainStatus(power_domain_status_t* out_status);
  zx_status_t PowerGetSupportedVoltageRange(uint32_t* min_voltage, uint32_t* max_voltage);
  zx_status_t PowerRequestVoltage(uint32_t voltage, uint32_t* actual_voltage);
  zx_status_t PowerGetCurrentVoltage(uint32_t index, uint32_t* current_voltage);
  zx_status_t PowerWritePmicCtrlReg(uint32_t reg_addr, uint32_t value);
  zx_status_t PowerReadPmicCtrlReg(uint32_t reg_addr, uint32_t* out_value);
  uint64_t component_device_id() const { return component_device_id_; }
  power_protocol_ops_t* ops() { return &power_protocol_ops_; }
  uint32_t min_needed_voltage_uV() const { return min_needed_voltage_uV_; }
  uint32_t max_supported_voltage_uV() const { return max_supported_voltage_uV_; }
  void set_min_needed_voltage_uV(uint32_t voltage) { min_needed_voltage_uV_ = voltage; }
  void set_max_supported_voltage_uV(uint32_t voltage) { max_supported_voltage_uV_ = voltage; }
  bool registered() const { return registered_; }
  void set_registered(bool value) { registered_ = value; }

 private:
  uint64_t component_device_id_;
  PowerDevice* power_device_;
  uint32_t min_needed_voltage_uV_ = 0;
  uint32_t max_supported_voltage_uV_ = 0;
  bool registered_ = false;
};

}  // namespace power

#endif  // SRC_DEVICES_POWER_DRIVERS_POWER_POWER_H_
