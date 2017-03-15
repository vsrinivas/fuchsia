// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/hw/usb.h>

__BEGIN_CDECLS;

// This protocol is used for drivers that bind to USB peripheral functions

// protocol data for iotxns
typedef struct {
    uint8_t ep_address;     // bEndpointAddress from endpoint descriptor
} usb_function_protocol_data_t;

typedef struct {
    // return the descriptor list for the function
    // TODO(voydanoff) - descriptors will likely vary (different max packet sizes, etc)
    // depending on whether we are in low/full, high or super speed mode.
    // We will need to add a usb_speed_t argument to this callback.
    const usb_descriptor_header_t* (*get_descriptors)(void* ctx, size_t* out_length);
    // callback for handling ep0 control requests
    mx_status_t (*control)(void* ctx, const usb_setup_t* setup, void* buffer, size_t buffer_length,
                           size_t* out_actual);
} usb_function_interface_ops_t;

typedef struct {
    usb_function_interface_ops_t* ops;
    void* ctx;
} usb_function_interface_t;

static inline const usb_descriptor_header_t* usb_function_get_descriptors(
                                        usb_function_interface_t* intf, size_t* out_length) {
    return intf->ops->get_descriptors(intf->ctx, out_length);
}

static inline mx_status_t usb_function_control(usb_function_interface_t* intf,
                                               const usb_setup_t* setup, void* buffer,
                                               size_t buffer_length, size_t* out_actual) {
    return intf->ops->control(intf->ctx, setup, buffer, buffer_length, out_actual);
}

typedef struct {
    mx_status_t (*register_func)(void* ctx, usb_function_interface_t* intf);
    uint8_t (*get_interface_number)(void* ctx);
    mx_status_t (*alloc_endpoint)(void* ctx, uint8_t direction, uint8_t* out_address);
    void (*queue)(void* ctx, iotxn_t* txn, uint8_t ep_address);
} usb_function_protocol_ops_t;

typedef struct {
    usb_function_protocol_ops_t* ops;
    void* ctx;
} usb_function_protocol_t;

// register's driver interface with the controller driver
static inline void usb_function_register(usb_function_protocol_t* func,
                                         usb_function_interface_t* intf) {
    func->ops->register_func(func->ctx, intf);
}

static inline uint8_t usb_function_get_interface_number(usb_function_protocol_t* func) {
    return func->ops->get_interface_number(func->ctx);
}

static inline mx_status_t usb_function_alloc_endpoint(usb_function_protocol_t* func,
                                                      uint8_t direction, uint8_t* out_address) {
    return func->ops->alloc_endpoint(func->ctx, direction, out_address);
}

static inline void usb_function_queue(usb_function_protocol_t* func, iotxn_t* txn,
                                      uint8_t ep_address) {
    return func->ops->queue(func->ctx, txn, ep_address);
}

__END_CDECLS;
