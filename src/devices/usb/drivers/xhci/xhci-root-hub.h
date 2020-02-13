// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_ROOT_HUB_H_
#define SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_ROOT_HUB_H_

#include <zircon/hw/usb/hub.h>
#include <zircon/listnode.h>

#include <ddk/protocol/usb/request.h>
#include <fbl/array.h>

#include "xhci-trb.h"

namespace usb_xhci {

struct xhci_t;

// this struct contains state needed for a virtual root hub device
struct xhci_root_hub_t {
  uint8_t num_ports;

  // port status for each of our ports
  fbl::Array<usb_port_status_t> port_status;

  // maps our virtual port index to actual root hub port index
  fbl::Array<uint8_t> port_map;

  // interrupt requests we have pending from hub driver
  list_node_t pending_intr_reqs;

  // device_desc and config_desc point into static data in xhci-root-hub.cpp
  const usb_device_descriptor_t* device_desc = nullptr;
  const usb_configuration_descriptor_t* config_desc = nullptr;
  usb_speed_t speed;
};

zx_status_t xhci_root_hub_init(xhci_t* xhci, int rh_index);
zx_status_t xhci_start_root_hubs(xhci_t* xhci);
void xhci_stop_root_hubs(xhci_t* xhci);
zx_status_t xhci_rh_usb_request_queue(xhci_t* xhci, usb_request_t* req, int rh_index);
void xhci_handle_root_hub_change(xhci_t* xhci);

}  // namespace usb_xhci

#endif  // SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_ROOT_HUB_H_
