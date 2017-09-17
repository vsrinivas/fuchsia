// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/iotxn.h>
#include <zircon/listnode.h>

#include "xhci-trb.h"

// this struct contains state needed for a virtual root hub device
typedef struct {
    uint32_t num_ports;

    // port status for each of our ports
    usb_port_status_t*  port_status;

    // maps our virtual port index to actual root hub port index
    uint8_t* port_map;

    // interrupt requests we have pending from hub driver
    list_node_t pending_intr_reqs;

    const usb_device_descriptor_t* device_desc;
    const usb_configuration_descriptor_t* config_desc;
    usb_speed_t speed;
} xhci_root_hub_t;

zx_status_t xhci_root_hub_init(xhci_t* xhci, int rh_index);
void xhci_root_hub_free(xhci_root_hub_t* rh);
zx_status_t xhci_start_root_hubs(xhci_t* xhci);
void xhci_stop_root_hubs(xhci_t* xhci);
zx_status_t xhci_rh_iotxn_queue(xhci_t* xhci, iotxn_t* txn, int rh_index);
void xhci_handle_root_hub_change(xhci_t* xhci);
