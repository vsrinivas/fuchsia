// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_HUB_H_
#define SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_HUB_H_

#include <fuchsia/hardware/usb/descriptor/cpp/banjo.h>
#include <zircon/hw/usb.h>

#include <fbl/slab_allocator.h>

namespace usb_xhci {
// This does need to be arena-allocated since it is freed in interrupt context
// and we don't have a context-aware allocator.
struct HubInfo {
  uint8_t hub_id = 0;
  usb_speed_t speed = 0;
  uint32_t route_string = 0;
  uint8_t hub_depth = 0;
  usb_speed_t hub_speed = 0;
  bool multi_tt = false;
  uint8_t rh_port = 0;
  uint8_t port_to_device[256];
  uint8_t parent_port_number = 0;
};
}  // namespace usb_xhci

#endif  // SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_HUB_H_
