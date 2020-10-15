// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_THERMAL_DRIVERS_AS370_THERMAL_AS370_THERMAL_H_
#define SRC_DEVICES_THERMAL_DRIVERS_AS370_THERMAL_AS370_THERMAL_H_

#include <fuchsia/hardware/thermal/llcpp/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/mmio/mmio.h>

#include <ddktl/device.h>
#include <ddktl/protocol/clock.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/power.h>

namespace thermal {

using llcpp::fuchsia::hardware::thermal::PowerDomain;
using llcpp::fuchsia::hardware::thermal::ThermalDeviceInfo;

class As370Thermal;
using DeviceType = ddk::Device<As370Thermal, ddk::Messageable, ddk::Unbindable>;

class As370Thermal : public DeviceType,
                     public ddk::EmptyProtocol<ZX_PROTOCOL_THERMAL>,
                     public llcpp::fuchsia::hardware::thermal::Device::Interface {
 public:
  As370Thermal(zx_device_t* parent, ddk::MmioBuffer mmio, const ThermalDeviceInfo& device_info,
               const ddk::ClockProtocolClient& cpu_clock, const ddk::PowerProtocolClient& cpu_power)
      : DeviceType(parent),
        mmio_(std::move(mmio)),
        device_info_(device_info),
        cpu_clock_(cpu_clock),
        cpu_power_(cpu_power) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);

  // Visible for testing.
  void GetInfo(GetInfoCompleter::Sync& completer) override;
  void GetDeviceInfo(GetDeviceInfoCompleter::Sync& completer) override;
  void GetDvfsInfo(PowerDomain power_domain, GetDvfsInfoCompleter::Sync& completer) override;
  void GetTemperatureCelsius(GetTemperatureCelsiusCompleter::Sync& completer) override;
  void GetStateChangeEvent(GetStateChangeEventCompleter::Sync& completer) override;
  void GetStateChangePort(GetStateChangePortCompleter::Sync& completer) override;
  void SetTripCelsius(uint32_t id, float temp, SetTripCelsiusCompleter::Sync& completer) override;
  void GetDvfsOperatingPoint(PowerDomain power_domain,
                             GetDvfsOperatingPointCompleter::Sync& completer) override;
  void SetDvfsOperatingPoint(uint16_t op_idx, PowerDomain power_domain,
                             SetDvfsOperatingPointCompleter::Sync& completer) override;
  void GetFanLevel(GetFanLevelCompleter::Sync& completer) override;
  void SetFanLevel(uint32_t fan_level, SetFanLevelCompleter::Sync& completer) override;

 private:
  zx_status_t Init();

  zx_status_t SetOperatingPoint(uint16_t op_idx);

  const ddk::MmioBuffer mmio_;
  const ThermalDeviceInfo device_info_;
  const ddk::ClockProtocolClient cpu_clock_;
  const ddk::PowerProtocolClient cpu_power_;
  uint16_t operating_point_ = 0;
};

}  // namespace thermal

#endif  // SRC_DEVICES_THERMAL_DRIVERS_AS370_THERMAL_AS370_THERMAL_H_
