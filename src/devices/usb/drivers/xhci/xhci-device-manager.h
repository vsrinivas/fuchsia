// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_DEVICE_MANAGER_H_
#define SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_DEVICE_MANAGER_H_

#include <fuchsia/hardware/usb/bus/c/banjo.h>
#include <fuchsia/hardware/usb/descriptor/c/banjo.h>
#include <fuchsia/hardware/usb/hubdescriptor/c/banjo.h>
#include <stdbool.h>
#include <zircon/hw/usb.h>
#include <zircon/types.h>

namespace usb_xhci {

struct xhci_t;

zx_status_t xhci_enumerate_device(xhci_t* xhci, uint32_t hub_address, uint32_t port,
                                  usb_speed_t speed);
zx_status_t xhci_device_disconnected(xhci_t* xhci, uint32_t hub_address, uint32_t port);
zx_status_t xhci_device_reset(xhci_t* xhci, uint32_t hub_address, uint32_t port);
void xhci_start_device_thread(xhci_t* xhci);
void xhci_stop_device_thread(xhci_t* xhci);
zx_status_t xhci_queue_start_root_hubs(xhci_t* xhci);
zx_status_t xhci_enable_endpoint(xhci_t* xhci, uint32_t slot_id,
                                 const usb_endpoint_descriptor_t* ep_desc,
                                 const usb_ss_ep_comp_descriptor_t* ss_comp_desc);
zx_status_t xhci_disable_endpoint(xhci_t* xhci, uint32_t slot_id,
                                  const usb_endpoint_descriptor_t* ep_desc);
zx_status_t xhci_configure_hub(xhci_t* xhci, uint32_t slot_id, usb_speed_t speed,
                               const usb_hub_descriptor_t* descriptor);

}  // namespace usb_xhci

#endif  // SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_DEVICE_MANAGER_H_
