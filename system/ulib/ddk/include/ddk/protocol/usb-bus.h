// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once


#include <ddk/driver.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb-hub.h>
#include <ddk/protocol/usb-hub.h>

__BEGIN_CDECLS;

typedef struct usb_bus_protocol_ops {
    // Hub support
    zx_status_t (*configure_hub)(void* ctx, zx_device_t* hub_device, usb_speed_t speed,
                 usb_hub_descriptor_t* descriptor);
    zx_status_t (*hub_device_added)(void* ctx, zx_device_t* hub_device, int port,
                                    usb_speed_t speed);
    zx_status_t (*hub_device_removed)(void* ctx, zx_device_t* hub_device, int port);
    zx_status_t (*set_hub_interface)(void* ctx, zx_device_t* usb_device, usb_hub_interface_t* hub);
} usb_bus_protocol_ops_t;

typedef struct usb_bus_protocol {
    usb_bus_protocol_ops_t* ops;
    void* ctx;
} usb_bus_protocol_t;

static inline zx_status_t usb_bus_configure_hub(usb_bus_protocol_t* bus, zx_device_t* hub_device,
                                                usb_speed_t speed,
                                                usb_hub_descriptor_t* descriptor) {
    return bus->ops->configure_hub(bus->ctx, hub_device, speed, descriptor);
}

static inline zx_status_t usb_bus_hub_device_added(usb_bus_protocol_t* bus, zx_device_t* hub_device,
                                                   int port, usb_speed_t speed) {
    return bus->ops->hub_device_added(bus->ctx, hub_device, port, speed);
}

static inline zx_status_t usb_bus_hub_device_removed(usb_bus_protocol_t* bus,
                                                     zx_device_t* hub_device, int port) {
    return bus->ops->hub_device_removed(bus->ctx, hub_device, port);
}

static inline zx_status_t usb_bus_set_hub_interface(usb_bus_protocol_t* bus,
                                                    zx_device_t* usb_device, usb_hub_interface_t* hub) {
    return bus->ops->set_hub_interface(bus->ctx, usb_device, hub);
}

// interface for use by the HCI controller to use to notify when devices are added and removed
typedef struct {
    zx_status_t (*add_device)(void* ctx, uint32_t device_id, uint32_t hub_id, usb_speed_t speed);
    void (*remove_device)(void* ctx, uint32_t device_id);
    void (*reset_hub_port)(void* ctx, uint32_t hub_id, uint32_t port);
} usb_bus_interface_ops_t;

typedef struct usb_bus_interface {
    usb_bus_interface_ops_t* ops;
    void* ctx;
} usb_bus_interface_t;

static inline zx_status_t usb_bus_add_device(usb_bus_interface_t* bus, uint32_t device_id,
                                             uint32_t hub_id, usb_speed_t speed) {
    return bus->ops->add_device(bus->ctx, device_id, hub_id, speed);
}

static inline void usb_bus_remove_device(usb_bus_interface_t* bus, uint32_t device_id) {
    bus->ops->remove_device(bus->ctx, device_id);
}

static inline void usb_bus_reset_hub_port(usb_bus_interface_t* bus, uint32_t hub_id, uint32_t port) {
    bus->ops->reset_hub_port(bus->ctx, hub_id, port);
}

__END_CDECLS;
