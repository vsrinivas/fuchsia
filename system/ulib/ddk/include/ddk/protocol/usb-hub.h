// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <ddk/driver.h>
#include <ddk/protocol/usb_device.h>

typedef struct usb_hub_protocol {
    /* returns 1 if the port's status changed since the last call */
    int (*port_status_changed)(mx_device_t* dev, int port);
    /* returns 1 if something is connected to the port */
    int (*port_connected)(mx_device_t* dev, int port);
    /* returns 1 if the port is enabled */
    int (*port_enabled)(mx_device_t* dev, int port);
    /* returns speed if port is enabled, negative value if not */
    usb_speed (*port_speed)(mx_device_t* dev, int port);

    /* enables (powers up) a port (optional) */
    int (*enable_port)(mx_device_t* dev, int port);
    /* disables (powers down) a port (optional) */
    int (*disable_port)(mx_device_t* dev, int port);

    /* performs a port reset (optional, generic implementations below) */
    int (*reset_port)(mx_device_t* dev, int port);

    int (*get_num_ports)(mx_device_t* dev);
} usb_hub_protocol_t;
