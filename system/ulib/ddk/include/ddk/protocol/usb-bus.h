// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once


#include <ddk/driver.h>
#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/hw/usb.h>
#include <magenta/hw/usb-hub.h>

__BEGIN_CDECLS;

typedef struct usb_bus_protocol_ops {
    // Hub support
    mx_status_t (*configure_hub)(void* ctx, mx_device_t* hub_device, usb_speed_t speed,
                 usb_hub_descriptor_t* descriptor);
    mx_status_t (*hub_device_added)(void* ctx, mx_device_t* hub_device, int port,
                                    usb_speed_t speed);
    mx_status_t (*hub_device_removed)(void* ctx, mx_device_t* hub_device, int port);
} usb_bus_protocol_ops_t;

typedef struct usb_bus_protocol {
    usb_bus_protocol_ops_t* ops;
    void* ctx;
} usb_bus_protocol_t;

static inline mx_status_t usb_bus_configure_hub(usb_bus_protocol_t* bus, mx_device_t* hub_device,
                                                usb_speed_t speed,
                                                usb_hub_descriptor_t* descriptor) {
    return bus->ops->configure_hub(bus->ctx, hub_device, speed, descriptor);
}

static inline mx_status_t usb_bus_hub_device_added(usb_bus_protocol_t* bus, mx_device_t* hub_device,
                                                   int port, usb_speed_t speed) {
    return bus->ops->hub_device_added(bus->ctx, hub_device, port, speed);
}

static inline mx_status_t usb_bus_hub_device_removed(usb_bus_protocol_t* bus,
                                                     mx_device_t* hub_device, int port) {
    return bus->ops->hub_device_removed(bus->ctx, hub_device, port);
}

// interface for use by the HCI controller to use to notify when devices are added and removed
typedef struct {
    mx_status_t (*add_device)(void* ctx, uint32_t device_id, uint32_t hub_id, usb_speed_t speed);
    void (*remove_device)(void* ctx, uint32_t device_id);
} usb_bus_interface_ops_t;

typedef struct usb_bus_interface {
    usb_bus_interface_ops_t* ops;
    void* ctx;
} usb_bus_interface_t;

static inline mx_status_t usb_bus_add_device(usb_bus_interface_t* bus, uint32_t device_id,
                                             uint32_t hub_id, usb_speed_t speed) {
    return bus->ops->add_device(bus->ctx, device_id, hub_id, speed);
}

static inline void usb_bus_remove_device(usb_bus_interface_t* bus, uint32_t device_id) {
    bus->ops->remove_device(bus->ctx, device_id);
}

__END_CDECLS;
