// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <hw/usb.h>
#include <hw/usb-hub.h>

// protocol data for iotxns
typedef struct usb_protocol_data {
    usb_setup_t setup;      // for control transactions
    uint32_t device_id;
    uint8_t ep_address;     // bEndpointAddress from endpoint descriptor
} usb_protocol_data_t;

// ******* FIXME Everything below this line should get moved to a private header file *******

typedef struct usb_device_protocol {
    // These are only used by hub driver
    mx_status_t (*configure_hub)(mx_device_t* device, usb_speed_t speed,
                                    usb_hub_descriptor_t* descriptor);
    mx_status_t (*hub_device_added)(mx_device_t* device, int port, usb_speed_t speed);
    mx_status_t (*hub_device_removed)(mx_device_t* device, int port);
} usb_device_protocol_t;

// For use by HCI controller drivers
mx_status_t usb_add_device(mx_device_t* hcidev, int address, usb_speed_t speed,
                           usb_device_descriptor_t* device_descriptor,
                           usb_configuration_descriptor_t** config_descriptors,
                           mx_device_t** out_device);
