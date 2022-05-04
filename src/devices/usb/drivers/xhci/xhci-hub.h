// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_HUB_H_
#define SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_HUB_H_

#include <fuchsia/hardware/usb/descriptor/cpp/banjo.h>
#include <zircon/hw/usb.h>

#include <optional>

#include <fbl/slab_allocator.h>

namespace usb_xhci {

// Record of the information needed to set up devices behind a TT. See comments within struct for
// required information.
struct tt_info_t {
  // tt_slot_id: the SlotId of the High Speed Hub that has the TT and interfaces with the full/low
  // speed device/hub environment. Set in PARENT_HUB_SLOT_ID of slot context.
  uint8_t tt_slot_id;
  // tt_port_number: the port number of the High Speed Hub that the full/low speed device/hub
  // environment is connected behind. Set in PARENT_PORT_NUMBER of slot context.
  uint8_t tt_port_number;
};

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
  // Should only exist for hubs behind the TT.
  std::optional<tt_info_t> tt_info;
};
}  // namespace usb_xhci

#endif  // SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_HUB_H_
