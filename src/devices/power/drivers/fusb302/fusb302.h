// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_FUSB302_FUSB302_H_
#define SRC_DEVICES_POWER_DRIVERS_FUSB302_FUSB302_H_

#include <fuchsia/hardware/power/llcpp/fidl.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>

namespace fusb302 {

class Fusb302;
using DeviceType = ddk::Device<Fusb302, ddk::Messageable<fuchsia_hardware_power::Source>::Mixin>;

class Fusb302 : public DeviceType {
 public:
  explicit Fusb302(zx_device_t* parent, ddk::I2cChannel i2c) : DeviceType(parent), i2c_(i2c) {}

  static zx_status_t Create(void* context, zx_device_t* device);

  void DdkRelease() { delete this; }

  // TODO (rdzhuang): change power FIDL to supply required values in SourceInfo
  void GetPowerInfo(GetPowerInfoRequestView request,
                    GetPowerInfoCompleter::Sync& completer) override {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
  }
  void GetStateChangeEvent(GetStateChangeEventRequestView request,
                           GetStateChangeEventCompleter::Sync& completer) override {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
  }
  void GetBatteryInfo(GetBatteryInfoRequestView request,
                      GetBatteryInfoCompleter::Sync& completer) override {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
  }

 private:
  friend class Fusb302Test;

  zx_status_t Init();

  zx_status_t Read(uint8_t reg_addr, uint8_t* out_buf, size_t len);
  zx_status_t Write(uint8_t reg_addr, uint8_t* buf, size_t len);

  inspect::Inspector inspect_;
  inspect::Node device_id_;
  inspect::StringProperty power_role_;
  inspect::StringProperty data_role_;
  inspect::DoubleProperty meas_vbus_;
  inspect::Node cc1_;
  inspect::DoubleProperty meas_cc1_;
  inspect::StringProperty bc_lvl_cc1_;
  inspect::Node cc2_;
  inspect::DoubleProperty meas_cc2_;
  inspect::StringProperty bc_lvl_cc2_;

  ddk::I2cChannel i2c_;
};

}  // namespace fusb302

#endif  // SRC_DEVICES_POWER_DRIVERS_FUSB302_FUSB302_H_
