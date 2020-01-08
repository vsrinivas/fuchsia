// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

namespace usb_xhci {

struct PortState {
  // Whether or not this is a USB 3.0 port. If this bit is set to false,
  // the other fields in this struct are undefined.
  bool is_USB3 = false;

  // Whether or not a device is currently attached to the PHY.
  bool is_connected = false;

  // Whether or not the link layer is active for this device.
  // Software may not attempt to communicate with the attached device
  // unless this is true.
  bool link_active = false;

  // Whether or not enumeration should be retried for this port.
  bool retry = false;

  // The primary slot connected to this port.
  // For hubs, this would be the slot ID of the hub
  // (not any peripherals attached to the hub)
  uint8_t slot_id;
};

}  // namespace usb_xhci
