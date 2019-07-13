// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_USB_VIRTUAL_BUS_USB_VIRTUAL_BUS_H_
#define ZIRCON_SYSTEM_UTEST_USB_VIRTUAL_BUS_USB_VIRTUAL_BUS_H_

#include <fbl/string.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/usb-virtual-bus-launcher/usb-virtual-bus-launcher.h>
#include <zircon/types.h>

namespace usb_virtual_bus {

using driver_integration_test::IsolatedDevmgr;

class USBVirtualBus : public usb_virtual_bus_base::USBVirtualBusBase {
 public:
  USBVirtualBus();

  // Initialize UMS. Asserts on failure.
  void InitUMS(fbl::String* devpath);

  // Initialize a Usb HID device. Asserts on failure.
  void InitUsbHid(fbl::String* devpath);

  // Initialize an FTDI device. Asserts on failure.
  void InitFtdi(fbl::String* devpath);
};

}  // namespace usb_virtual_bus

#endif  // ZIRCON_SYSTEM_UTEST_USB_VIRTUAL_BUS_USB_VIRTUAL_BUS_H_
