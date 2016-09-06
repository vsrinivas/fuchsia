// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/hw/usb-hub.h>
#include <magenta/types.h>

typedef struct xhci xhci_t;

mx_status_t xhci_enumerate_device(xhci_t* xhci, uint32_t hub_address, uint32_t port, usb_speed_t speed);
mx_status_t xhci_device_disconnected(xhci_t* xhci, uint32_t hub_address, uint32_t port);
void xhci_start_device_thread(xhci_t* xhci);
mx_status_t xhci_configure_hub(xhci_t* xhci, int slot_id, usb_speed_t speed,
                               usb_hub_descriptor_t* descriptor);
mx_status_t xhci_rh_port_connected(xhci_t* xhci, uint32_t port);
mx_status_t xhci_rh_port_disconnected(xhci_t* xhci, uint32_t port);
