// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/protocol/usb-device.h>
#include <hw/usb-hub.h>

typedef struct usb_hci_protocol {
    usb_request_t* (*alloc_request)(mx_device_t* dev, uint16_t size);
    void (*free_request)(mx_device_t* dev, usb_request_t* request);

    int (*queue_request)(mx_device_t* hcidev, int devaddr, usb_request_t* request);

    // These are only used by hub driver
    mx_status_t (*configure_hub)(mx_device_t* dev, int devaddr, usb_speed_t speed,
                 usb_hub_descriptor_t* descriptor);
    mx_status_t (*hub_device_added)(mx_device_t* dev, int devaddr, int port, usb_speed_t speed);
    mx_status_t (*hub_device_removed)(mx_device_t* dev, int devaddr, int port);
} usb_hci_protocol_t;
