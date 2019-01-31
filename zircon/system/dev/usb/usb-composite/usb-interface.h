// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <zircon/hw/usb.h>

#include "usb-composite.h"

// Represents an interface within a composite device
typedef struct {
    zx_device_t* zxdev;
    usb_composite_t* comp;

    // ID of the last interface in the descriptor list.
    uint8_t last_interface_id;
    usb_descriptor_header_t* descriptor;
    size_t descriptor_length;
    // descriptors for currently active endpoints
    usb_endpoint_descriptor_t* active_endpoints[USB_MAX_EPS];

    // node for usb_composite_t "children" list
    list_node_t node;
} usb_interface_t;

extern usb_protocol_ops_t usb_device_protocol;
extern usb_composite_protocol_ops_t usb_composite_device_protocol;
extern zx_protocol_device_t usb_interface_proto;

bool usb_interface_contains_interface(usb_interface_t* intf, uint8_t interface_id);

zx_status_t usb_interface_set_alt_setting(usb_interface_t* intf, uint8_t interface_id,
                                          uint8_t alt_setting);

zx_status_t usb_interface_configure_endpoints(usb_interface_t* intf, uint8_t interface_id,
                                              uint8_t alt_setting);
