// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef USB_VIRTUAL_BUS_LAUNCHER_USB_VIRTUAL_BUS_LAUNCHER_H_
#define USB_VIRTUAL_BUS_LAUNCHER_USB_VIRTUAL_BUS_LAUNCHER_H_

#include <fbl/string.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/usb-virtual-bus-launcher-helper/usb-virtual-bus-launcher-helper.h>
#include <zircon/types.h>

namespace usb_virtual_bus_base {

using driver_integration_test::IsolatedDevmgr;

class USBVirtualBusBase {
 public:
  USBVirtualBusBase();
  void GetHandles(zx::unowned_channel* peripheral, zx::unowned_channel* bus);
  int GetRootFd();

 protected:
  IsolatedDevmgr::Args args_;
  IsolatedDevmgr devmgr_;
  zx::channel peripheral_;
  zx::channel virtual_bus_handle_;
  DISALLOW_COPY_ASSIGN_AND_MOVE(USBVirtualBusBase);
};

}  // namespace usb_virtual_bus_base

#endif  // USB_VIRTUAL_BUS_LAUNCHER_USB_VIRTUAL_BUS_LAUNCHER_H_
