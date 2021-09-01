// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ISOLATED_DEVMGR_USB_VIRTUAL_BUS_H_
#define SRC_LIB_ISOLATED_DEVMGR_USB_VIRTUAL_BUS_H_

#include <fidl/fuchsia.hardware.usb.peripheral/cpp/wire.h>
#include <fidl/fuchsia.hardware.usb.virtual.bus/cpp/wire.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/types.h>

#include <optional>
#include <vector>

#include <fbl/unique_fd.h>

#include "usb-virtual-bus-helper.h"

namespace usb_virtual_bus {
using fuchsia_hardware_usb_peripheral::wire::DeviceDescriptor;
using fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor;
using ConfigurationDescriptor =
    ::fidl::VectorView<fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor>;

class USBVirtualBusBase {
 public:
  USBVirtualBusBase(std::string pkg_url, std::string svc_name);
  void InitPeripheral();
  void SetupPeripheralDevice(DeviceDescriptor&& device_desc,
                             std::vector<FunctionDescriptor> function_descs);
  void ClearPeripheralDeviceFunctions();
  int GetRootFd();
  fbl::unique_fd& devfs_root() { return devfs_; };
  fidl::WireSyncClient<fuchsia_hardware_usb_peripheral::Device>& peripheral() {
    return peripheral_.value();
  }
  fidl::WireSyncClient<fuchsia_hardware_usb_virtual_bus::Bus>& virtual_bus() {
    return virtual_bus_.value();
  }

 private:
  fbl::unique_fd devfs_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  fidl::InterfacePtr<fuchsia::sys::ComponentController> ctlr_;
  std::optional<fidl::WireSyncClient<fuchsia_hardware_usb_peripheral::Device>> peripheral_;
  std::optional<fidl::WireSyncClient<fuchsia_hardware_usb_virtual_bus::Bus>> virtual_bus_;
  DISALLOW_COPY_ASSIGN_AND_MOVE(USBVirtualBusBase);
};

}  // namespace usb_virtual_bus

#endif  // SRC_LIB_ISOLATED_DEVMGR_USB_VIRTUAL_BUS_H_
