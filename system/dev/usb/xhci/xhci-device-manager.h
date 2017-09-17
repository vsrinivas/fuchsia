// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/hw/usb-hub.h>
#include <zircon/types.h>
#include <stdbool.h>

typedef struct xhci xhci_t;

zx_status_t xhci_enumerate_device(xhci_t* xhci, uint32_t hub_address, uint32_t port,
                                  usb_speed_t speed);
zx_status_t xhci_device_disconnected(xhci_t* xhci, uint32_t hub_address, uint32_t port);
void xhci_start_device_thread(xhci_t* xhci);
void xhci_stop_device_thread(xhci_t* xhci);
zx_status_t xhci_queue_start_root_hubs(xhci_t* xhci);
zx_status_t xhci_enable_endpoint(xhci_t* xhci, uint32_t slot_id, usb_endpoint_descriptor_t* ep_desc,
                                 usb_ss_ep_comp_descriptor_t* ss_comp_desc, bool enable);
zx_status_t xhci_configure_hub(xhci_t* xhci, uint32_t slot_id, usb_speed_t speed,
                               usb_hub_descriptor_t* descriptor);
