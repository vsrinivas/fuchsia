// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/protocol/usb-device.h>

typedef struct usb_hub_protocol {
    /* returns 1 if the port's status changed since the last call */
    int (*port_status_changed)(mx_device_t* dev, int port);
    /* returns 1 if something is connected to the port */
    int (*port_connected)(mx_device_t* dev, int port);
    /* returns 1 if the port is enabled */
    int (*port_enabled)(mx_device_t* dev, int port);
    /* returns speed if port is enabled, negative value if not */
    usb_speed_t (*port_speed)(mx_device_t* dev, int port);

    /* enables (powers up) a port (optional) */
    int (*enable_port)(mx_device_t* dev, int port);
    /* disables (powers down) a port (optional) */
    int (*disable_port)(mx_device_t* dev, int port);

    /* performs a port reset (optional, generic implementations below) */
    int (*reset_port)(mx_device_t* dev, int port);

    int (*get_num_ports)(mx_device_t* dev);
} usb_hub_protocol_t;
