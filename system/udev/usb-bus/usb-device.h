// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/protocol/usb-hci.h>
#include <magenta/hw/usb.h>

// Represents a USB top-level device
typedef struct usb_device {
    mx_device_t device;

    // ID assigned by host controller
    uint32_t device_id;
    // device_id of the hub we are attached to (or zero for root hub)
    uint32_t hub_id;
    usb_speed_t speed;

    mx_device_t* hci_device;
    usb_hci_protocol_t* hci_protocol;

    usb_device_descriptor_t device_desc;
    usb_configuration_descriptor_t* config_desc;

    mx_device_prop_t props[7];

    // list of child devices (for USB composite devices)
    list_node_t children;

    list_node_t node;
} usb_device_t;
#define get_usb_device(dev) containerof(dev, usb_device_t, device)

mx_status_t usb_device_add(mx_device_t* hci_device, usb_hci_protocol_t* hci_protocol,
                           mx_device_t* parent,  uint32_t device_id, uint32_t hub_id,
                           usb_speed_t speed, usb_device_t** out_device);

void usb_device_remove(usb_device_t* dev);
