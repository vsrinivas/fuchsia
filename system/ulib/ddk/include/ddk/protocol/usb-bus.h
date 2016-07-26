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
#include <ddk/protocol/usb-device.h>

typedef struct usb_bus_protocol {
    mx_device_t* (*attach_device)(mx_device_t* busdev, mx_device_t* hubdev, int hubaddress, int port,
                                  usb_speed_t speed);
    void (*detach_device)(mx_device_t* busdev, mx_device_t* dev);
    void (*root_hub_port_changed)(mx_device_t* busdev, int port);

} usb_bus_protocol_t;
