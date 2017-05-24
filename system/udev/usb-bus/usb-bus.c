// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/protocol/usb.h>
#include <ddk/protocol/usb-bus.h>
#include <ddk/protocol/usb-hci.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "usb-device.h"
#include "usb-interface.h"

// Represents a USB bus, which manages all devices for a USB host controller
typedef struct usb_bus {
    mx_device_t* mxdev;
    mx_device_t* hci_mxdev;
    usb_hci_protocol_t* hci_protocol;

    // top-level USB devices, indexed by device_id
    usb_device_t** devices;
    size_t max_device_count;
} usb_bus_t;

static mx_status_t usb_bus_add_device(mx_device_t* device, uint32_t device_id, uint32_t hub_id,
                                      usb_speed_t speed) {
    usb_bus_t* bus = device->ctx;

    if (device_id >= bus->max_device_count) return ERR_INVALID_ARGS;

    usb_device_t* usb_device;
    mx_status_t result = usb_device_add(bus->hci_mxdev, bus->hci_protocol, bus->mxdev, device_id,
                                        hub_id, speed, &usb_device);
    if (result == NO_ERROR) {
        bus->devices[device_id] = usb_device;
    }
    return result;
}

static void usb_bus_remove_device(mx_device_t* dev, uint32_t device_id) {
    usb_bus_t* bus = dev->ctx;
    if (device_id >= bus->max_device_count) {
        printf("device_id out of range in usb_bus_remove_device\n");
        return;
    }
    usb_device_t* device = bus->devices[device_id];
    if (device) {
        usb_device_remove(device);
        bus->devices[device_id] = NULL;
    }
}

static mx_status_t usb_bus_configure_hub(mx_device_t* device, mx_device_t* hub_device, usb_speed_t speed,
                                         usb_hub_descriptor_t* descriptor) {
    usb_bus_t* bus = device->ctx;
    uint32_t hub_id = usb_interface_get_device_id(hub_device);
    return bus->hci_protocol->configure_hub(bus->hci_mxdev, hub_id, speed, descriptor);
}

static mx_status_t usb_bus_device_added(mx_device_t* device, mx_device_t* hub_device, int port, usb_speed_t speed) {
    usb_bus_t* bus = device->ctx;
    uint32_t hub_id = usb_interface_get_device_id(hub_device);
    return bus->hci_protocol->hub_device_added(bus->hci_mxdev, hub_id, port, speed);
}

static mx_status_t usb_bus_device_removed(mx_device_t* device, mx_device_t* hub_device, int port) {
    usb_bus_t* bus = device->ctx;
    uint32_t hub_id = usb_interface_get_device_id(hub_device);
    return bus->hci_protocol->hub_device_removed(bus->hci_mxdev, hub_id, port);
}

static usb_bus_protocol_t _bus_protocol = {
    .add_device = usb_bus_add_device,
    .remove_device = usb_bus_remove_device,
    .configure_hub = usb_bus_configure_hub,
    .hub_device_added = usb_bus_device_added,
    .hub_device_removed = usb_bus_device_removed,
};

static void usb_bus_unbind(void* ctx) {
    usb_bus_t* bus = ctx;
    bus->hci_protocol->set_bus_device(bus->hci_mxdev, NULL);

    for (size_t i = 0; i < bus->max_device_count; i++) {
        usb_device_t* device = bus->devices[i];
        if (device) {
            device_remove(device->mxdev);
            bus->devices[i] = NULL;
        }
    }
}

static void usb_bus_release(void* ctx) {
    usb_bus_t* bus = ctx;
    free(bus->devices);
    free(bus);
}

static mx_protocol_device_t usb_bus_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = usb_bus_unbind,
    .release = usb_bus_release,
};

static mx_status_t usb_bus_bind(void* ctx, mx_device_t* device, void** cookie) {
    usb_hci_protocol_t* hci_protocol;
    if (device_op_get_protocol(device, MX_PROTOCOL_USB_HCI, (void**)&hci_protocol)) {
        return ERR_NOT_SUPPORTED;
    }

    usb_bus_t* bus = calloc(1, sizeof(usb_bus_t));
    if (!bus) {
        printf("Not enough memory for usb_bus_t.\n");
        return ERR_NO_MEMORY;
    }

    bus->hci_mxdev = device;
    bus->hci_protocol = hci_protocol;

    bus->max_device_count = hci_protocol->get_max_device_count(device);
    bus->devices = calloc(bus->max_device_count, sizeof(usb_device_t *));
    if (!bus->devices) {
        printf("Not enough memory for usb_bus_t->devices. max_device_count: %zu\n",
               bus->max_device_count);
        free(bus);
        return ERR_NO_MEMORY;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "usb_bus",
        .ctx = bus,
        .ops = &usb_bus_device_proto,
        .proto_id = MX_PROTOCOL_USB_BUS,
        .proto_ops = &_bus_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    mx_status_t status = device_add(device, &args, &bus->mxdev);
    if (status == NO_ERROR) {
        hci_protocol->set_bus_device(device, bus->mxdev);
    } else {
        free(bus->devices);
        free(bus);
    }

    return status;
}

static mx_driver_ops_t usb_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_bus_bind,
};

MAGENTA_DRIVER_BEGIN(usb_bus, usb_bus_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_USB_HCI),
MAGENTA_DRIVER_END(usb_bus)
