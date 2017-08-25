// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/iotxn.h>
#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/hw/usb.h>

__BEGIN_CDECLS;

// This protocol is used for USB peripheral function functions

// protocol data for iotxns
typedef struct {
    uint8_t ep_address;     // bEndpointAddress from endpoint descriptor
} usb_function_protocol_data_t;

// callbacks implemented by the function driver
typedef struct {
    // return the descriptor list for the function
    // TODO(voydanoff) - descriptors will likely vary (different max packet sizes, etc)
    // depending on whether we are in low/full, high or super speed mode.
    // We will need to add a usb_speed_t argument to this callback.
    const usb_descriptor_header_t* (*get_descriptors)(void* ctx, size_t* out_length);

    // callback for handling ep0 control requests
    mx_status_t (*control)(void* ctx, const usb_setup_t* setup, void* buffer, size_t buffer_length,
                           size_t* out_actual);

    // Called to inform the function driver when the USB device configured state changes.
    // Called with configured == true in response to a SET_CONFIGURATION control request
    // that selects a configuration that contains this function. In this case, the function driver
    // should call usb_function_config_ep() to configure its endpoints.
    // Called with configured == false when configuration is disabled or USB is disconnected.
    // The function driver should then call usb_function_disable_ep() to disable its endpoints.
    mx_status_t (*set_configured)(void* ctx, bool configured, usb_speed_t speed);

    // Called to set an alternate setting for an interface due to a SET_INTERFACE control request.
    // The function driver should call usb_function_config_ep() and/or usb_function_config_ep()
    // to configure or disable the interface's endpoints as appropriate.
    mx_status_t (*set_interface)(void* ctx, unsigned interface, unsigned alt_setting);
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

static inline mx_status_t usb_function_set_configured(usb_function_interface_t* intf,
                                                      bool configured, usb_speed_t speed) {
    return intf->ops->set_configured(intf->ctx, configured, speed);
}

static inline mx_status_t usb_function_set_interface(usb_function_interface_t* intf,
                                                     unsigned interface, unsigned alt_setting) {
    return intf->ops->set_interface(intf->ctx, interface, alt_setting);
}

typedef struct {
    mx_status_t (*register_func)(void* ctx, usb_function_interface_t* intf);
    mx_status_t (*alloc_interface)(void* ctx, uint8_t* out_intf_num);
    mx_status_t (*alloc_ep)(void* ctx, uint8_t direction, uint8_t* out_address);
    mx_status_t (*config_ep)(void* ctx, usb_endpoint_descriptor_t* ep_desc,
                             usb_ss_ep_comp_descriptor_t* ss_comp_desc);
    mx_status_t (*disable_ep)(void* ctx, uint8_t ep_addr);
    mx_status_t (*alloc_string_desc)(void* ctx, const char* string, uint8_t* out_index);
    void (*queue)(void* ctx, iotxn_t* txn, uint8_t ep_address);
    mx_status_t (*ep_set_stall)(void* ctx, uint8_t ep_address);
    mx_status_t (*ep_clear_stall)(void* ctx, uint8_t ep_address);
} usb_function_protocol_ops_t;

typedef struct {
    usb_function_protocol_ops_t* ops;
    void* ctx;
} usb_function_protocol_t;

// registers the function driver's callback interface
static inline void usb_function_register(usb_function_protocol_t* func,
                                         usb_function_interface_t* intf) {
    func->ops->register_func(func->ctx, intf);
}

// allocates a unique interface descriptor number
static inline mx_status_t usb_function_alloc_interface(usb_function_protocol_t* func,
                                                       uint8_t* out_intf_num) {
    return func->ops->alloc_interface(func->ctx, out_intf_num);
}

// allocates a unique endpoint descriptor number
// direction should be either USB_DIR_OUT or USB_DIR_IN
static inline mx_status_t usb_function_alloc_ep(usb_function_protocol_t* func, uint8_t direction,
                                                uint8_t* out_address) {
    return func->ops->alloc_ep(func->ctx, direction, out_address);
}

// configures an endpoint based on the provided usb_endpoint_descriptor_t and
// usb_ss_ep_comp_descriptor_t descriptors.
static inline mx_status_t usb_function_config_ep(usb_function_protocol_t* func,
                                                 usb_endpoint_descriptor_t* ep_desc,
                                                 usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    return func->ops->config_ep(func->ctx, ep_desc, ss_comp_desc);
}

// disables an endpoint. called when the device is no longer configured or an alternate interface
// is selected.
static inline mx_status_t usb_function_disable_ep(usb_function_protocol_t* func, uint8_t ep_addr) {
    return func->ops->disable_ep(func->ctx, ep_addr);
}

// adds a string descriptor to the device configuration.
static inline mx_status_t usb_function_alloc_string_desc(usb_function_protocol_t* func,
                                                         const char* string, uint8_t* out_index) {
    return func->ops->alloc_string_desc(func->ctx, string, out_index);
}

// helper for queueing an iotxn on an endpoint.
static inline void usb_function_queue(usb_function_protocol_t* func, iotxn_t* txn,
                                      uint8_t ep_address) {
    return func->ops->queue(func->ctx, txn, ep_address);
}

// stalls an endpoint
static mx_status_t usb_function_ep_set_stall(usb_function_protocol_t* func, uint8_t ep_address) {
    return func->ops->ep_set_stall(func->ctx, ep_address);
}

// clears endpoint stall state
static mx_status_t usb_function_ep_clear_stall(usb_function_protocol_t* func, uint8_t ep_address) {
    return func->ops->ep_clear_stall(func->ctx, ep_address);
}

__END_CDECLS;
