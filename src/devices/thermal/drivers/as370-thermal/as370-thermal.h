// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_THERMAL_DRIVERS_AS370_THERMAL_AS370_THERMAL_H_
#define SRC_DEVICES_THERMAL_DRIVERS_AS370_THERMAL_AS370_THERMAL_H_

#include <fuchsia/hardware/clock/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/power/cpp/banjo.h>
#include <fuchsia/hardware/thermal/llcpp/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/mmio/mmio.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace thermal {

using fuchsia_hardware_thermal::wire::PowerDomain;
using fuchsia_hardware_thermal::wire::ThermalDeviceInfo;

class As370Thermal;
using DeviceType = ddk::Device<As370Thermal, ddk::MessageableOld, ddk::Unbindable>;

class As370Thermal : public DeviceType,
                     public ddk::EmptyProtocol<ZX_PROTOCOL_THERMAL>,
                     public fidl::WireServer<fuchsia_hardware_thermal::Device> {
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
  void GetInfo(GetInfoRequestView request, GetInfoCompleter::Sync& completer) override;
  void GetDeviceInfo(GetDeviceInfoRequestView request,
                     GetDeviceInfoCompleter::Sync& completer) override;
  void GetDvfsInfo(GetDvfsInfoRequestView request, GetDvfsInfoCompleter::Sync& completer) override;
  void GetTemperatureCelsius(GetTemperatureCelsiusRequestView request,
                             GetTemperatureCelsiusCompleter::Sync& completer) override;
  void GetStateChangeEvent(GetStateChangeEventRequestView request,
                           GetStateChangeEventCompleter::Sync& completer) override;
  void GetStateChangePort(GetStateChangePortRequestView request,
                          GetStateChangePortCompleter::Sync& completer) override;
  void SetTripCelsius(SetTripCelsiusRequestView request,
                      SetTripCelsiusCompleter::Sync& completer) override;
  void GetDvfsOperatingPoint(GetDvfsOperatingPointRequestView request,
                             GetDvfsOperatingPointCompleter::Sync& completer) override;
  void SetDvfsOperatingPoint(SetDvfsOperatingPointRequestView request,
                             SetDvfsOperatingPointCompleter::Sync& completer) override;
  void GetFanLevel(GetFanLevelRequestView request, GetFanLevelCompleter::Sync& completer) override;
  void SetFanLevel(SetFanLevelRequestView request, SetFanLevelCompleter::Sync& completer) override;

  zx_status_t Init();

 private:
  zx_status_t SetOperatingPoint(uint16_t op_idx);

  const ddk::MmioBuffer mmio_;
  const ThermalDeviceInfo device_info_;
  const ddk::ClockProtocolClient cpu_clock_;
  const ddk::PowerProtocolClient cpu_power_;
  uint16_t operating_point_ = 0;
};

}  // namespace thermal

#endif  // SRC_DEVICES_THERMAL_DRIVERS_AS370_THERMAL_AS370_THERMAL_H_
