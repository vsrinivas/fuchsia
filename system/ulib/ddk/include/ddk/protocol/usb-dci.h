// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/hw/usb.h>

__BEGIN_CDECLS;

// This protocol is used for drivers that bind to USB peripheral controllers
typedef struct {
    // callback for handling ep0 control requests
    mx_status_t (*control)(void* ctx, const usb_setup_t* setup, void* buffer, size_t buffer_length,
                           size_t* out_actual);
} usb_dci_interface_ops_t;

typedef struct {
    usb_dci_interface_ops_t* ops;
    void* ctx;
} usb_dci_interface_t;

static inline mx_status_t usb_dci_control(usb_dci_interface_t* intf, const usb_setup_t* setup,
                                          void* buffer, size_t buffer_length, size_t* out_actual) {
    return intf->ops->control(intf->ctx, setup, buffer, buffer_length, out_actual);
}

typedef struct {
    mx_status_t (*set_interface)(void* ctx, usb_dci_interface_t* interface);
    mx_status_t (*config_ep)(void* ctx, const usb_endpoint_descriptor_t* ep_desc);
    // enables or disables the device controller hardware
    // should not be enabled until upper layer is ready to respond to the host
    mx_status_t (*set_enabled)(void* ctx, bool enabled);
} usb_dci_protocol_ops_t;

typedef struct {
    usb_dci_protocol_ops_t* ops;
    void* ctx;
} usb_dci_protocol_t;

// register's driver interface with the controller driver
static inline void usb_dci_set_interface(usb_dci_protocol_t* device, usb_dci_interface_t* intf) {
    device->ops->set_interface(device->ctx, intf);
}

static mx_status_t usb_dci_config_ep(usb_dci_protocol_t* device,
                                     const usb_endpoint_descriptor_t* ep_desc) {
    return device->ops->config_ep(device->ctx, ep_desc);
}

static mx_status_t usb_dci_set_enabled(usb_dci_protocol_t* device, bool enabled) {
    return device->ops->set_enabled(device->ctx, enabled);
}

__END_CDECLS;
