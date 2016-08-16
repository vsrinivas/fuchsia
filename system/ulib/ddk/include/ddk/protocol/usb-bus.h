// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/protocol/usb-device.h>

typedef struct usb_bus_protocol {
    mx_device_t* (*attach_device)(mx_device_t* busdev, mx_device_t* hubdev, int hubaddress, int port,
                                  usb_speed_t speed);
    void (*detach_device)(mx_device_t* busdev, mx_device_t* dev);
    void (*root_hub_port_changed)(mx_device_t* busdev, int port);

} usb_bus_protocol_t;
