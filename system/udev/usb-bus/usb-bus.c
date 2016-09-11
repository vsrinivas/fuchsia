// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/protocol/usb-bus.h>
#include <ddk/protocol/usb-hci.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "usb-device.h"

// Represents a USB bus, which manages all devices for a USB host controller
typedef struct usb_bus {
    mx_device_t device;

    mx_device_t* hci_device;
    usb_hci_protocol_t* hci_protocol;

    // top-level USB devices, indexed by device_id
    usb_device_t** devices;
    size_t max_device_count;
} usb_bus_t;
#define get_usb_bus(dev) containerof(dev, usb_bus_t, device)

mx_status_t usb_bus_add_device(mx_device_t* device, uint32_t device_id, uint32_t hub_id,
                               usb_speed_t speed, usb_device_descriptor_t* device_descriptor,
                               usb_configuration_descriptor_t** config_descriptors) {
    usb_bus_t* bus = get_usb_bus(device);

    if (!device_descriptor || !config_descriptors) return ERR_INVALID_ARGS;
    if (device_id >= bus->max_device_count) return ERR_INVALID_ARGS;

    usb_device_t* usb_device;
    mx_status_t result = usb_device_add(bus->hci_device, &bus->device, device_id, hub_id, speed,
                                        device_descriptor, config_descriptors, &usb_device);
    if (result == NO_ERROR) {
        bus->devices[device_id] = usb_device;
    }
    return result;
}

static void usb_bus_do_remove_device(usb_bus_t* bus, uint32_t device_id) {
    if (device_id >= bus->max_device_count) {
        printf("device_id out of range in usb_bus_remove_device\n");
        return;
    }
    usb_device_t* device = bus->devices[device_id];
    if (device) {
        // if this is a hub, recursively remove any devices attached to it
        for (size_t i = 0; i < bus->max_device_count; i++) {
            usb_device_t* child = bus->devices[i];
            if (child && child->hub_id == device_id) {
                usb_bus_do_remove_device(bus, i);
            }
        }

        usb_device_remove(device);
        bus->devices[device_id] = NULL;
    }
}

static void usb_bus_remove_device(mx_device_t* device, uint32_t device_id) {
    usb_bus_t* bus = get_usb_bus(device);
    usb_bus_do_remove_device(bus, device_id);
}

static mx_status_t usb_bus_configure_hub(mx_device_t* device, mx_device_t* hub_device, usb_speed_t speed,
                                         usb_hub_descriptor_t* descriptor) {
    usb_bus_t* bus = get_usb_bus(device);
    usb_device_t* dev = get_usb_device(hub_device);
    return bus->hci_protocol->configure_hub(bus->hci_device, dev->device_id, speed, descriptor);
}

static mx_status_t usb_bus_device_added(mx_device_t* device, mx_device_t* hub_device, int port, usb_speed_t speed) {
    usb_bus_t* bus = get_usb_bus(device);
    usb_device_t* dev = get_usb_device(hub_device);
    return bus->hci_protocol->hub_device_added(bus->hci_device, dev->device_id, port, speed);
}

static mx_status_t usb_bus_device_removed(mx_device_t* device, mx_device_t* hub_device, int port) {
    usb_bus_t* bus = get_usb_bus(device);
    usb_device_t* dev = get_usb_device(hub_device);
    return bus->hci_protocol->hub_device_removed(bus->hci_device, dev->device_id, port);
}

static usb_bus_protocol_t _bus_protocol = {
    .add_device = usb_bus_add_device,
    .remove_device = usb_bus_remove_device,
    .configure_hub = usb_bus_configure_hub,
    .hub_device_added = usb_bus_device_added,
    .hub_device_removed = usb_bus_device_removed,
};

static void usb_bus_unbind(mx_device_t* dev) {
    usb_bus_t* bus = get_usb_bus(dev);
    bus->hci_protocol->set_bus_device(bus->hci_device, NULL);

    for (size_t i = 0; i < bus->max_device_count; i++) {
        usb_device_t* device = bus->devices[i];
        if (device) {
            device_remove(&device->device);
            bus->devices[i] = NULL;
        }
    }
}

static mx_status_t usb_bus_release(mx_device_t* dev) {
    usb_bus_t* bus = get_usb_bus(dev);
    free(bus->devices);
    free(bus);
    return NO_ERROR;
}

static mx_protocol_device_t usb_bus_device_proto = {
    .unbind = usb_bus_unbind,
    .release = usb_bus_release,
};

static mx_status_t usb_bus_bind(mx_driver_t* driver, mx_device_t* device) {
    usb_hci_protocol_t* hci_protocol;
    if (device_get_protocol(device, MX_PROTOCOL_USB_HCI, (void**)&hci_protocol)) {
        return ERR_NOT_SUPPORTED;
    }

    usb_bus_t* bus = calloc(1, sizeof(usb_bus_t));
    if (!bus) {
        printf("Not enough memory for usb_bus_t.\n");
        return ERR_NO_MEMORY;
    }

    bus->hci_device = device;
    bus->hci_protocol = hci_protocol;

    bus->max_device_count = hci_protocol->get_max_device_count(device);
    bus->devices = calloc(bus->max_device_count, sizeof(usb_device_t *));
    if (!bus->devices) {
        printf("Not enough memory for usb_bus_t->devices. max_device_count: %zu\n",
               bus->max_device_count);
        free(bus);
        return ERR_NO_MEMORY;
    }

    device_init(&bus->device, driver, "usb_bus", &usb_bus_device_proto);

    bus->device.protocol_id = MX_PROTOCOL_USB_BUS;
    bus->device.protocol_ops = &_bus_protocol;
    device_set_bindable(&bus->device, false);
    mx_status_t status = device_add(&bus->device, device);
    if (status == NO_ERROR) {
        hci_protocol->set_bus_device(device, &bus->device);
    } else {
        free(bus->devices);
        free(bus);
    }

    return status;
}

static mx_bind_inst_t binding[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_USB_HCI),
};

mx_driver_t _driver_usb_bus BUILTIN_DRIVER = {
    .name = "usb_bus",
    .ops = {
        .bind = usb_bus_bind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
