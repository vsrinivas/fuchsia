// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_ACPI_DRIVERS_ACPI_PWRSRC_ACPI_PWRSRC_H_
#define SRC_DEVICES_ACPI_DRIVERS_ACPI_PWRSRC_ACPI_PWRSRC_H_

#include <fidl/fuchsia.hardware.acpi/cpp/markers.h>
#include <fidl/fuchsia.hardware.power/cpp/wire.h>
#include <fidl/fuchsia.hardware.power/cpp/wire_types.h>
#include <lib/async/dispatcher.h>
#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>

#include "src/devices/lib/acpi/client.h"

namespace acpi_pwrsrc {

inline constexpr uint32_t kPowerSourceStateChanged = 0x80;

class AcpiPwrsrc;
using DeviceType = ddk::Device<AcpiPwrsrc, ddk::Initializable,
                               ddk::Messageable<fuchsia_hardware_power::Source>::Mixin>;
class AcpiPwrsrc : public DeviceType, fidl::WireServer<fuchsia_hardware_acpi::NotifyHandler> {
 public:
  explicit AcpiPwrsrc(zx_device_t* parent, acpi::Client acpi, async_dispatcher_t* dispatcher)
      : DeviceType(parent), acpi_(std::move(acpi)), dispatcher_(dispatcher) {}
  virtual ~AcpiPwrsrc() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

  // FIDL methods.
  void GetPowerInfo(GetPowerInfoRequestView request,
                    GetPowerInfoCompleter::Sync& completer) override;
  void GetStateChangeEvent(GetStateChangeEventRequestView request,
                           GetStateChangeEventCompleter::Sync& completer) override;
  void GetBatteryInfo(GetBatteryInfoRequestView request,
                      GetBatteryInfoCompleter::Sync& completer) override;

  void Handle(HandleRequestView request, HandleCompleter::Sync& completer) override;

 private:
  acpi::Client acpi_;
  async_dispatcher_t* dispatcher_ = nullptr;

  zx::event state_event_;
  bool online_ __TA_GUARDED(lock_) = false;
  std::mutex lock_;

  // Call _PSR to see if this device is online, and update online_ and state_event_ if necessary.
  zx_status_t CheckOnline() __TA_EXCLUDES(lock_);
};

}  // namespace acpi_pwrsrc

#endif  // SRC_DEVICES_ACPI_DRIVERS_ACPI_PWRSRC_ACPI_PWRSRC_H_
