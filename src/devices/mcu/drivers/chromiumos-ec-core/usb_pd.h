// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MCU_DRIVERS_CHROMIUMOS_EC_CORE_USB_PD_H_
#define SRC_DEVICES_MCU_DRIVERS_CHROMIUMOS_EC_CORE_USB_PD_H_

#include <fidl/fuchsia.hardware.power/cpp/wire.h>
#include <lib/zx/event.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <chromiumos-platform-ec/ec_commands.h>
#include <ddktl/device.h>

#include "src/devices/mcu/drivers/chromiumos-ec-core/chromiumos_ec_core.h"

namespace chromiumos_ec_core::usb_pd {

class AcpiCrOsEcUsbPdDevice;

using AcpiCrOsEcUsbPdDeviceType =
    ddk::Device<AcpiCrOsEcUsbPdDevice, ddk::Messageable<fuchsia_hardware_power::Source>::Mixin,
                ddk::Initializable>;

enum PortState {
  kCharging,
  kNotCharging,
};

class AcpiCrOsEcUsbPdDevice : public AcpiCrOsEcUsbPdDeviceType {
 public:
  // Create and bind the device.
  //
  // A pointer to the created device will be placed in |device|, though ownership
  // remains with the DDK. Any use of |device| must occur before DdkRelease()
  // is called.
  static zx_status_t Bind(zx_device_t* parent, ChromiumosEcCore* ec,
                          AcpiCrOsEcUsbPdDevice** device);

  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

  // fuchsia.hardware.power methods
  void GetPowerInfo(GetPowerInfoRequestView request,
                    GetPowerInfoCompleter::Sync& completer) override;
  void GetStateChangeEvent(GetStateChangeEventRequestView request,
                           GetStateChangeEventCompleter::Sync& completer) override;
  void GetBatteryInfo(GetBatteryInfoRequestView request,
                      GetBatteryInfoCompleter::Sync& completer) override;

  void NotifyHandler(uint32_t value);

 private:
  // ACPI device notifications range from 0x80-0xFF. The USB PD device gets notifications with the
  // first device-specific notification value (0x80), which is overloaded on the EC to notify the
  // other EC connected devices (such as the motion sensor.)
  static constexpr uint32_t kPowerChangedNotification = 0x80;

  AcpiCrOsEcUsbPdDevice(ChromiumosEcCore* ec, zx_device_t* parent, zx::event event)
      : AcpiCrOsEcUsbPdDeviceType(parent), ec_(ec), event_(std::move(event)) {}
  DISALLOW_COPY_ASSIGN_AND_MOVE(AcpiCrOsEcUsbPdDevice);

  void HandleEvent();
  fpromise::promise<bool, zx_status_t> UpdateState();
  fpromise::promise<void, zx_status_t> GetPorts();

  ChromiumosEcCore* ec_;
  zx::event event_;
  std::optional<ChromiumosEcCore::NotifyHandlerDeleter> notify_deleter_;

  std::vector<PortState> ports_;
};

}  // namespace chromiumos_ec_core::usb_pd

#endif  // SRC_DEVICES_MCU_DRIVERS_CHROMIUMOS_EC_CORE_USB_PD_H_
