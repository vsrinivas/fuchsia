// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/hw/usb.h>
#include <magenta/hw/usb-hub.h>

__BEGIN_CDECLS;

typedef struct iotxn iotxn_t;

// protocol data for iotxns
typedef struct usb_protocol_data {
    usb_setup_t setup;      // for control transactions
    uint64_t frame;         // frame number for scheduling isochronous transfers
    uint32_t device_id;
    uint8_t ep_address;     // bEndpointAddress from endpoint descriptor
} usb_protocol_data_t;

typedef struct usb_protocol_ops {
    mx_status_t (*control)(void* ctx, uint8_t request_type, uint8_t request, uint16_t value,
                           uint16_t index, void* data, size_t length, mx_time_t timeout);
    void (*queue)(void* ctx, iotxn_t* txn, uint8_t ep_address, uint64_t frame);
    usb_speed_t (*get_speed)(void* ctx);
    mx_status_t (*set_interface)(void* ctx, int interface_number, int alt_setting);
    mx_status_t (*set_configuration)(void* ctx, int configuration);
    mx_status_t (*reset_endpoint)(void* ctx, uint8_t ep_address);
    size_t (*get_max_transfer_size)(void* ctx, uint8_t ep_address);
    uint32_t (*get_device_id)(void* ctx);
    mx_status_t (*get_descriptor_list)(void* ctx, void** out_descriptors, size_t* out_length);
    mx_status_t (*get_additional_descriptor_list)(void* ctx, void** out_descriptors,
                                                  size_t* out_length);
    mx_status_t (*claim_interface)(void* ctx, usb_interface_descriptor_t* intf, size_t length);
    mx_status_t (*cancel_all)(void* ctx, uint8_t ep_address);
} usb_protocol_ops_t;

typedef struct usb_protocol {
    usb_protocol_ops_t* ops;
    void* ctx;
} usb_protocol_t;

// synchronously executes a control request on endpoint zero
static inline mx_status_t usb_control(usb_protocol_t* usb, uint8_t request_type, uint8_t request,
                                      uint16_t value, uint16_t index, void* data, size_t length,
                                      mx_time_t timeout) {
    return usb->ops->control(usb->ctx, request_type, request, value, index, data, length, timeout);
}

static inline mx_status_t usb_get_descriptor(usb_protocol_t* usb, uint8_t request_type,
                                             uint16_t type, uint16_t index, void* data,
                                             size_t length, mx_time_t timeout) {
    return usb_control(usb, request_type | USB_DIR_IN, USB_REQ_GET_DESCRIPTOR, type << 8 | index, 0,
                       data, length, timeout);
}

static inline mx_status_t usb_get_status(usb_protocol_t* usb, uint8_t request_type, uint16_t index,
                                         void* data, size_t length, mx_time_t timeout) {
    return usb_control(usb, request_type | USB_DIR_IN, USB_REQ_GET_STATUS, 0, index, data, length,
                       timeout);
}

static inline mx_status_t usb_set_feature(usb_protocol_t* usb, uint8_t request_type, int feature,
                                          int index, mx_time_t timeout) {
    return usb_control(usb, request_type, USB_REQ_SET_FEATURE, feature, index, NULL, 0, timeout);
}

static inline mx_status_t usb_clear_feature(usb_protocol_t* usb, uint8_t request_type, int feature,
                                            int index, mx_time_t timeout) {
    return usb_control(usb, request_type, USB_REQ_CLEAR_FEATURE, feature, index, NULL, 0, timeout);
}

static inline void usb_queue(usb_protocol_t* usb, iotxn_t* txn, uint8_t ep_address) {
    return usb->ops->queue(usb->ctx, txn, ep_address, 0);
}

static inline void usb_queue_isoch(usb_protocol_t* usb, iotxn_t* txn, uint8_t ep_address,
                                   uint64_t frame) {
    return usb->ops->queue(usb->ctx, txn, ep_address, frame);
}

static inline usb_speed_t usb_get_speed(usb_protocol_t* usb) {
    return usb->ops->get_speed(usb->ctx);
}

static inline mx_status_t usb_set_interface(usb_protocol_t* usb, int interface_number,
                                            int alt_setting) {
    return usb->ops->set_interface(usb->ctx, interface_number, alt_setting);
}

static inline mx_status_t usb_set_configuration(usb_protocol_t* usb, int configuration) {
    return usb->ops->set_configuration(usb->ctx, configuration);
}

// Resets an endpoint that is in a halted or error state.
// Endpoints will be halted if the device returns a STALL in response to a USB transaction.
// When that occurs, the transaction will fail with ERR_IO_REFUSED.
// usb_reset_endpoint() the endpoint to normal running state.
static inline mx_status_t usb_reset_endpoint(usb_protocol_t* usb, uint8_t ep_address) {
    return usb->ops->reset_endpoint(usb->ctx, ep_address);
}

// returns the maximum amount of data that can be transferred on an endpoint in a single transaction.
static inline mx_status_t usb_get_max_transfer_size(usb_protocol_t* usb, uint8_t ep_address) {
    return usb->ops->get_max_transfer_size(usb->ctx, ep_address);
}

static inline mx_status_t usb_get_device_id(usb_protocol_t* usb) {
    return usb->ops->get_device_id(usb->ctx);
}

// returns the USB descriptors for the USB device or interface
// the returned value is de-allocated with free()
static inline mx_status_t usb_get_descriptor_list(usb_protocol_t* usb, void** out_descriptors,
                                                  size_t* out_length) {
    return usb->ops->get_descriptor_list(usb->ctx, out_descriptors, out_length);
}

// returns the USB descriptors following the interface's existing descriptors
// the returned value is de-allocated with free()
static inline mx_status_t usb_get_additional_descriptor_list(usb_protocol_t* usb,
                                                             void** out_descriptors,
                                                             size_t* out_length) {
    return usb->ops->get_additional_descriptor_list(usb->ctx, out_descriptors, out_length);
}

// marks the interface as claimed and appends the interface descriptor to the
// interface's existing descriptors.
static inline mx_status_t usb_claim_interface(usb_protocol_t* usb,
                                              usb_interface_descriptor_t* intf, size_t length) {
    return usb->ops->claim_interface(usb->ctx, intf, length);
}

static inline mx_status_t usb_cancel_all(usb_protocol_t* usb, uint8_t ep_address) {
    return usb->ops->cancel_all(usb->ctx, ep_address);
}

__END_CDECLS;
