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

#include <magenta/types.h>
#include <mxu/list.h>
#include <runtime/compiler.h>
#include <sys/types.h>

typedef struct mx_device mx_device_t;
typedef struct mx_protocol_device mx_protocol_device_t;

typedef struct mx_driver mx_driver_t;
typedef struct mx_driver_binding mx_driver_binding_t;

typedef struct mx_driver_ops {
    mx_status_t (*init)(mx_driver_t* driver);
    // Opportunity to do on-load work.
    // Called ony once, before any other ops are called.

    mx_status_t (*probe)(mx_driver_t* driver, mx_device_t* device);
    // Query whether this driver can bind to this device.
    // NO_ERROR indicates it can do so.

    mx_status_t (*bind)(mx_driver_t* driver, mx_device_t* device);
    // Requests that the driver bind to the provided device,
    // initialize it, and publish and children.

    mx_status_t (*unbind)(mx_driver_t* driver, mx_device_t* device);
    // Notifies the driver that the device is being removed (has
    // been hot unplugged, etc)

    mx_status_t (*shutdown)(mx_driver_t* driver);
    // Requests that the driver un-publish children, release the devices
    // that it's bound to, etc.

    mx_status_t (*release)(mx_driver_t* driver);
    // Last call before driver is unloaded.
} mx_driver_ops_t;

struct mx_driver {
    const char* name;

    mx_driver_ops_t ops;

    struct list_node node;

    mx_driver_binding_t* binding;
    uint32_t binding_count;
    // list of protocols and options
};

struct mx_driver_binding {
    uint32_t protocol_id;
    void* protocol_info;
};

// Device Manager API
mx_status_t device_create(mx_device_t** device, mx_driver_t* driver,
                          const char* name, mx_protocol_device_t* ops);
mx_status_t device_init(mx_device_t* device, mx_driver_t* driver,
                        const char* name, mx_protocol_device_t* ops);
// Devices are created or (if embedded in a driver-specific structure)
// initialized with the above functions.  The mx_device_t will be completely
// written during initialization, and after initialization and before calling
// device_add() they driver may only modify the protocol_id and protocol_ops
// fields of the mx_device_t.

mx_status_t device_add(mx_device_t* device, mx_device_t* parent);
mx_status_t device_remove(mx_device_t* device);

// Devices are bindable by drivers by default.
// This can be used to prevent a device from being bound by a driver
void device_set_bindable(mx_device_t* dev, bool bindable);

void driver_add(mx_driver_t* driver);
void driver_remove(mx_driver_t* driver);

// Protocol Identifiers
#define MX_PROTOCOL_DEVICE 'pDEV'
#define MX_PROTOCOL_CHAR 'pCHR'
#define MX_PROTOCOL_CONSOLE 'pCON'
#define MX_PROTOCOL_DISPLAY 'pDIS'
#define MX_PROTOCOL_FB 'pFBU'
#define MX_PROTOCOL_PCI 'pPCI'
#define MX_PROTOCOL_USB_HCI 'pHCI'
#define MX_PROTOCOL_USB_BUS 'pUBS'
#define MX_PROTOCOL_USB_HUB 'pHUB'
#define MX_PROTOCOL_USB_DEVICE 'pUDV'
#define MX_PROTOCOL_ETHERNET 'pETH'
#define MX_PROTOCOL_BLUETOOTH_HCI 'pBTH'

#define BUILTIN_DRIVER       \
    __ALIGNED(sizeof(void*)) \
    __SECTION("builtin_drivers")
