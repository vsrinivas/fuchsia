// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_CROS_EC_USB_PD_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_CROS_EC_USB_PD_H_

#include <fidl/fuchsia.hardware.power/cpp/wire.h>
#include <lib/zx/event.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <acpica/acpi.h>
#include <chromiumos-platform-ec/ec_commands.h>
#include <ddktl/device.h>
#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/span.h>
#include <fbl/vector.h>

#include "acpi.h"
#include "dev.h"

class AcpiCrOsEcUsbPdDevice;

using AcpiCrOsEcUsbPdDeviceType =
    ddk::Device<AcpiCrOsEcUsbPdDevice, ddk::Messageable<fuchsia_hardware_power::Source>::Mixin>;

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
  static zx_status_t Bind(zx_device_t* parent, fbl::RefPtr<cros_ec::EmbeddedController> ec,
                          std::unique_ptr<cros_ec::AcpiHandle> acpi_handle,
                          AcpiCrOsEcUsbPdDevice** device);

  void DdkRelease();

  // fuchsia.hardware.power methods
  void GetPowerInfo(GetPowerInfoRequestView request,
                    GetPowerInfoCompleter::Sync& completer) override;
  void GetStateChangeEvent(GetStateChangeEventRequestView request,
                           GetStateChangeEventCompleter::Sync& completer) override;
  void GetBatteryInfo(GetBatteryInfoRequestView request,
                      GetBatteryInfoCompleter::Sync& completer) override;

  // Exposed for testing.
  static void NotifyHandler(ACPI_HANDLE handle, UINT32 value, void* ctx);

 private:
  // ACPI device notifications range from 0x80-0xFF. The USB PD device gets notifications with the
  // first device-specific notification value (0x80), which is overloaded on the EC to notify the
  // other EC connected devices (such as the motion sensor.)
  static constexpr uint32_t kPowerChangedNotification = 0x80;

  AcpiCrOsEcUsbPdDevice(fbl::RefPtr<cros_ec::EmbeddedController> ec, zx_device_t* parent,
                        std::unique_ptr<cros_ec::AcpiHandle> acpi_handle, zx::event event)
      : AcpiCrOsEcUsbPdDeviceType(parent),
        ec_(std::move(ec)),
        acpi_handle_(std::move(acpi_handle)),
        event_(std::move(event)) {}
  DISALLOW_COPY_ASSIGN_AND_MOVE(AcpiCrOsEcUsbPdDevice);

  zx_status_t HandleEvent();
  zx_status_t UpdateState(bool* changed = nullptr);
  zx_status_t GetPorts();

  fbl::RefPtr<cros_ec::EmbeddedController> ec_;
  std::unique_ptr<cros_ec::AcpiHandle> acpi_handle_;

  zx::event event_;

  std::vector<PortState> ports_;
};

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_CROS_EC_USB_PD_H_
