// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ISOLATED_DEVMGR_USB_VIRTUAL_BUS_H_
#define SRC_LIB_ISOLATED_DEVMGR_USB_VIRTUAL_BUS_H_

#include <fuchsia/hardware/usb/peripheral/llcpp/fidl.h>
#include <fuchsia/hardware/usb/virtual/bus/llcpp/fidl.h>
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
    ::fidl::VectorView<::fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor>;

class USBVirtualBusBase {
 public:
  USBVirtualBusBase(std::string pkg_url, std::string svc_name);
  void InitPeripheral();
  void SetupPeripheralDevice(DeviceDescriptor&& device_desc,
                             std::vector<FunctionDescriptor> function_descs);
  void ClearPeripheralDeviceFunctions();
  int GetRootFd();
  fbl::unique_fd& devfs_root() { return devfs_; };
  fuchsia_hardware_usb_peripheral::Device::SyncClient& peripheral() { return peripheral_.value(); }
  fuchsia_hardware_usb_virtual_bus::Bus::SyncClient& virtual_bus() { return virtual_bus_.value(); }

 private:
  fbl::unique_fd devfs_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  fidl::InterfacePtr<fuchsia::sys::ComponentController> ctlr_;
  std::optional<fuchsia_hardware_usb_peripheral::Device::SyncClient> peripheral_;
  std::optional<fuchsia_hardware_usb_virtual_bus::Bus::SyncClient> virtual_bus_;
  DISALLOW_COPY_ASSIGN_AND_MOVE(USBVirtualBusBase);
};

}  // namespace usb_virtual_bus

#endif  // SRC_LIB_ISOLATED_DEVMGR_USB_VIRTUAL_BUS_H_
