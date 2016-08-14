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
    int (*control)(mx_device_t* hcidev, int devaddr, usb_setup_t* devreq, int data_length,
                   uint8_t* data);

    /* set_address(): Tell the usb device its address
                      Also, allocate the usbdev structure, initialize enpoint 0
                      (including MPS) and return its address. */
    int (*set_address)(mx_device_t* hcidev, usb_speed_t speed, int hubport, int hubaddr);

    /* finish_device_config(): Another hook for xHCI, returns 0 on success. */
    int (*finish_device_config)(mx_device_t* hcidev, int devaddr, usb_device_config_t* config);

    /* destroy_device(): Finally, destroy all structures that were allocated during set_address()
                         and finish_device_config(). */
    void (*destroy_device)(mx_device_t* hcidev, int devaddr);

    void (*set_bus_device)(mx_device_t* hcidev, mx_device_t* busdev);

    // These are only used by hub driver
    mx_status_t (*configure_hub)(mx_device_t* hcidev, int devaddr, usb_speed_t speed,
                 usb_hub_descriptor_t* descriptor);
    mx_status_t (*hub_device_added)(mx_device_t* hcidev, int devaddr, int port, usb_speed_t speed);
    mx_status_t (*hub_device_removed)(mx_device_t* hcidev, int devaddr, int port);
} usb_hci_protocol_t;
