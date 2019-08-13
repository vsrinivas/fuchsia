// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef USB_VIRTUAL_BUS_LAUNCHER_USB_VIRTUAL_BUS_LAUNCHER_H_
#define USB_VIRTUAL_BUS_LAUNCHER_USB_VIRTUAL_BUS_LAUNCHER_H_

#include <fuchsia/hardware/usb/peripheral/llcpp/fidl.h>
#include <fuchsia/hardware/usb/virtual/bus/llcpp/fidl.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/usb-virtual-bus-launcher-helper/usb-virtual-bus-launcher-helper.h>
#include <zircon/types.h>

#include <optional>
#include <vector>

namespace usb_virtual_bus_base {

using driver_integration_test::IsolatedDevmgr;

using ::llcpp::fuchsia::hardware::usb::peripheral::DeviceDescriptor;
using ::llcpp::fuchsia::hardware::usb::peripheral::FunctionDescriptor;

class USBVirtualBusBase {
 public:
  USBVirtualBusBase();

  void SetupPeripheralDevice(const DeviceDescriptor& device_desc,
                             std::vector<FunctionDescriptor> function_descs);

  int GetRootFd();

  llcpp::fuchsia::hardware::usb::peripheral::Device::SyncClient& peripheral() {
    return peripheral_.value();
  }
  llcpp::fuchsia::hardware::usb::virtual_::bus::Bus::SyncClient& virtual_bus() {
    return virtual_bus_.value();
  }

 protected:
  IsolatedDevmgr::Args args_;
  IsolatedDevmgr devmgr_;

 private:
  std::optional<llcpp::fuchsia::hardware::usb::peripheral::Device::SyncClient> peripheral_;
  std::optional<llcpp::fuchsia::hardware::usb::virtual_::bus::Bus::SyncClient> virtual_bus_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(USBVirtualBusBase);
};

}  // namespace usb_virtual_bus_base

#endif  // USB_VIRTUAL_BUS_LAUNCHER_USB_VIRTUAL_BUS_LAUNCHER_H_
