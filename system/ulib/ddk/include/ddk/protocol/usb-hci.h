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
} usb_hci_protocol_t;
