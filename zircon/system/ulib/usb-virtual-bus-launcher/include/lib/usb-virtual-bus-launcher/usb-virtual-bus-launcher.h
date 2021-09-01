// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_USB_VIRTUAL_BUS_LAUNCHER_USB_VIRTUAL_BUS_LAUNCHER_H_
#define LIB_USB_VIRTUAL_BUS_LAUNCHER_USB_VIRTUAL_BUS_LAUNCHER_H_

#include <fidl/fuchsia.hardware.usb.peripheral/cpp/wire.h>
#include <fidl/fuchsia.hardware.usb.virtual.bus/cpp/wire.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/usb-virtual-bus-launcher-helper/usb-virtual-bus-launcher-helper.h>
#include <zircon/types.h>

#include <optional>
#include <vector>

namespace usb_virtual_bus_base {

using driver_integration_test::IsolatedDevmgr;

using ConfigurationDescriptor =
    ::fidl::VectorView<fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor>;
using fuchsia_hardware_usb_peripheral::wire::DeviceDescriptor;
using fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor;

class __EXPORT USBVirtualBusBase {
 public:
  USBVirtualBusBase();

  void SetupPeripheralDevice(DeviceDescriptor&& device_desc,
                             std::vector<ConfigurationDescriptor> function_descs);
  // Asks the peripheral device to clear its functions and waits for the FunctionsCleared event.
  void ClearPeripheralDeviceFunctions();

  int GetRootFd();

  fidl::WireSyncClient<fuchsia_hardware_usb_peripheral::Device>& peripheral() {
    return peripheral_.value();
  }
  fidl::WireSyncClient<fuchsia_hardware_usb_virtual_bus::Bus>& virtual_bus() {
    return virtual_bus_.value();
  }

  IsolatedDevmgr devmgr_;

 protected:
  IsolatedDevmgr::Args args_;

 private:
  std::optional<fidl::WireSyncClient<fuchsia_hardware_usb_peripheral::Device>> peripheral_;
  std::optional<fidl::WireSyncClient<fuchsia_hardware_usb_virtual_bus::Bus>> virtual_bus_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(USBVirtualBusBase);
};

}  // namespace usb_virtual_bus_base

#endif  // LIB_USB_VIRTUAL_BUS_LAUNCHER_USB_VIRTUAL_BUS_LAUNCHER_H_
