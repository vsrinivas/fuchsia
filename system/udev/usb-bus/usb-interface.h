// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <magenta/device/usb.h>
#include <magenta/hw/usb.h>

// Represents an interface within a composite device
typedef struct {
    mx_device_t device;

    mx_device_t* hci_device;
    usb_hci_protocol_t* hci_protocol;
    uint32_t device_id;

    usb_interface_descriptor_t* interface_desc;
    size_t interface_desc_length;
    // descriptors for currently active endpoints
    usb_endpoint_descriptor_t* active_endpoints[USB_MAX_EPS];

    mx_device_prop_t props[7];

    list_node_t node;
} usb_interface_t;
#define get_usb_interface(dev) containerof(dev, usb_interface_t, device)

// for determining index into active_endpoints[]
// bEndpointAddress has 4 lower order bits, plus high bit to signify direction
// shift high bit to bit 4 so index is in range 0 - 31.
#define get_usb_endpoint_index(ep) (((ep)->bEndpointAddress & 0x0F) | ((ep)->bEndpointAddress >> 3))

typedef struct usb_device usb_device_t;

mx_status_t usb_device_add_interface(usb_device_t* device,
                                     usb_device_descriptor_t* device_descriptor,
                                     usb_interface_descriptor_t* interface_desc,
                                     size_t interface_desc_length);

void usb_device_remove_interfaces(usb_device_t* device);

uint32_t usb_interface_get_device_id(mx_device_t* device);

bool usb_interface_contains_interface(usb_interface_t* intf, uint8_t interface_id);

mx_status_t usb_interface_set_alt_setting(usb_interface_t* intf, uint8_t interface_id,
                                          uint8_t alt_setting);
