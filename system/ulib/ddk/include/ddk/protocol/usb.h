// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/usb-request.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb-hub.h>

__BEGIN_CDECLS;

typedef struct usb_protocol_ops {
    zx_status_t (*req_alloc)(void* ctx, usb_request_t** out, uint64_t data_size,
                             uint8_t ep_address);
    zx_status_t (*req_alloc_vmo)(void* ctx, usb_request_t** out, zx_handle_t vmo_handle,
                                 uint64_t vmo_offset, uint64_t length, uint8_t ep_address);
    zx_status_t (*req_init)(void* ctx, usb_request_t* req, zx_handle_t vmo_handle,
                            uint64_t vmo_offset, uint64_t length, uint8_t ep_address);
    ssize_t (*req_copy_from)(void* ctx, usb_request_t* req, void* data,
                                 size_t length, size_t offset);
    ssize_t (*req_copy_to)(void* ctx, usb_request_t* req, const void* data,
                               size_t length, size_t offset);
    zx_status_t (*req_mmap)(void* ctx, usb_request_t* req, void** data);
    zx_status_t (*req_cacheop)(void* ctx, usb_request_t* req, uint32_t op,
                                   size_t offset, size_t length);
    zx_status_t (*req_cache_flush)(void* ctx, usb_request_t* req,
                                   size_t offset, size_t length);
    zx_status_t (*req_cache_flush_invalidate)(void* ctx, usb_request_t* req,
                                              zx_off_t offset, size_t length);
    zx_status_t (*req_physmap)(void* ctx, usb_request_t* req);
    void (*req_release)(void* ctx, usb_request_t* req);
    void (*req_complete)(void* ctx, usb_request_t* req, zx_status_t status, zx_off_t actual);
    void (*req_phys_iter_init)(void* ctx, phys_iter_t* iter, usb_request_t* req,
                                   size_t max_length);
    zx_status_t (*control)(void* ctx, uint8_t request_type, uint8_t request, uint16_t value,
                           uint16_t index, void* data, size_t length, zx_time_t timeout,
                           size_t* out_length);
    // queues a USB request
    void (*request_queue)(void* ctx, usb_request_t* usb_request);
    usb_speed_t (*get_speed)(void* ctx);
    zx_status_t (*set_interface)(void* ctx, int interface_number, int alt_setting);
    zx_status_t (*set_configuration)(void* ctx, int configuration);
    zx_status_t (*reset_endpoint)(void* ctx, uint8_t ep_address);
    size_t (*get_max_transfer_size)(void* ctx, uint8_t ep_address);
    uint32_t (*get_device_id)(void* ctx);
    void (*get_device_descriptor)(void* ctx, usb_device_descriptor_t* out_desc);
    zx_status_t (*get_descriptor_list)(void* ctx, void** out_descriptors, size_t* out_length);
    zx_status_t (*get_additional_descriptor_list)(void* ctx, void** out_descriptors,
                                                  size_t* out_length);
    zx_status_t (*get_string_descriptor)(void* ctx,
                                         uint8_t desc_id, uint16_t* inout_lang_id,
                                         uint8_t* buf, size_t* inout_buflen);
    zx_status_t (*claim_interface)(void* ctx, usb_interface_descriptor_t* intf, size_t length);
    zx_status_t (*cancel_all)(void* ctx, uint8_t ep_address);
} usb_protocol_ops_t;

typedef struct usb_protocol {
    usb_protocol_ops_t* ops;
    void* ctx;
} usb_protocol_t;

static inline zx_status_t usb_req_alloc(const usb_protocol_t* usb, usb_request_t** out,
                                        uint64_t data_size, uint8_t ep_address) {
    return usb->ops->req_alloc(usb->ctx, out, data_size, ep_address);
}

static inline zx_status_t usb_req_alloc_vmo(const usb_protocol_t* usb, usb_request_t** out,
                                             zx_handle_t vmo_handle, uint64_t vmo_offset,
                                             uint64_t length, uint8_t ep_address) {
    return usb->ops->req_alloc_vmo(usb->ctx, out, vmo_handle, vmo_offset, length, ep_address);
}

static inline zx_status_t usb_req_init(const usb_protocol_t* usb, usb_request_t* req,
                                       zx_handle_t vmo_handle, uint64_t vmo_offset,
                                       uint64_t length, uint8_t ep_address) {
    return usb->ops->req_init(usb->ctx, req, vmo_handle, vmo_offset, length, ep_address);
}

static inline ssize_t usb_req_copy_from(const usb_protocol_t* usb, usb_request_t* req, void* data,
                                        size_t length, size_t offset) {
    return usb->ops->req_copy_from(usb->ctx, req, data, length, offset);
}

static inline ssize_t usb_req_copy_to(const usb_protocol_t* usb, usb_request_t* req,
                                      const void* data, size_t length, size_t offset) {
    return usb->ops->req_copy_to(usb->ctx, req, data, length, offset);
}

static inline zx_status_t usb_req_mmap(const usb_protocol_t* usb, usb_request_t* req,
                                       void** data) {
    return usb->ops->req_mmap(usb->ctx, req, data);
}

static inline zx_status_t usb_req_cacheop(const usb_protocol_t* usb, usb_request_t* req,
                                          uint32_t op, size_t offset, size_t length) {
    return usb->ops->req_cacheop(usb->ctx, req, op, offset, length);
}

static inline zx_status_t usb_req_cache_flush(const usb_protocol_t* usb, usb_request_t* req,
                                              size_t offset, size_t length) {
    return usb->ops->req_cache_flush(usb->ctx, req, offset, length);
}

static inline zx_status_t usb_req_cache_flush_invalidate(const usb_protocol_t* usb,
                                                         usb_request_t* req, zx_off_t offset,
                                                         size_t length) {
    return usb->ops->req_cache_flush_invalidate(usb->ctx, req, offset, length);
}

static inline zx_status_t usb_req_physmap(const usb_protocol_t* usb, usb_request_t* req) {
    return usb->ops->req_physmap(usb->ctx, req);
}

static inline void usb_req_release(const usb_protocol_t* usb, usb_request_t* req) {
    usb->ops->req_release(usb->ctx, req);
}

static inline void usb_req_complete(const usb_protocol_t* usb, usb_request_t* req,
                             zx_status_t status, zx_off_t actual) {
    usb->ops->req_complete(usb->ctx, req, status, actual);
}

static inline void usb_req_phys_iter_init(const usb_protocol_t* usb, phys_iter_t* iter,
                                          usb_request_t* req, size_t max_length) {
    usb->ops->req_phys_iter_init(usb->ctx, iter, req, max_length);
}

// synchronously executes a control request on endpoint zero
static inline zx_status_t usb_control(const usb_protocol_t* usb, uint8_t request_type,
                                      uint8_t request, uint16_t value, uint16_t index, void* data,
                                      size_t length, zx_time_t timeout, size_t* out_length) {
    return usb->ops->control(usb->ctx, request_type, request, value, index, data, length, timeout,
                             out_length);
}

static inline zx_status_t usb_get_descriptor(const usb_protocol_t* usb, uint8_t request_type,
                                             uint16_t type, uint16_t index, void* data,
                                             size_t length, zx_time_t timeout, size_t* out_length) {
    return usb_control(usb, request_type | USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                       (uint16_t)(type << 8 | index), 0, data, length, timeout, out_length);
}

static inline zx_status_t usb_get_status(const usb_protocol_t* usb, uint8_t request_type,
                                         uint16_t index, void* data, size_t length,
                                         zx_time_t timeout, size_t* out_length) {
    return usb_control(usb, request_type | USB_DIR_IN, USB_REQ_GET_STATUS, 0, index, data, length,
                       timeout, out_length);
}

static inline zx_status_t usb_set_feature(const usb_protocol_t* usb, uint8_t request_type,
                                          uint16_t feature, uint16_t index, zx_time_t timeout) {
    return usb_control(usb, request_type, USB_REQ_SET_FEATURE, feature, index, NULL, 0, timeout,
                       NULL);
}

static inline zx_status_t usb_clear_feature(const usb_protocol_t* usb, uint8_t request_type,
                                            uint16_t feature, uint16_t index, zx_time_t timeout) {
    return usb_control(usb, request_type, USB_REQ_CLEAR_FEATURE, feature, index, NULL, 0, timeout,
                       NULL);
}

static inline void usb_request_queue(const usb_protocol_t* usb, usb_request_t* usb_request) {
    return usb->ops->request_queue(usb->ctx, usb_request);
}

static inline usb_speed_t usb_get_speed(const usb_protocol_t* usb) {
    return usb->ops->get_speed(usb->ctx);
}

static inline zx_status_t usb_set_interface(const usb_protocol_t* usb, int interface_number,
                                            int alt_setting) {
    return usb->ops->set_interface(usb->ctx, interface_number, alt_setting);
}

static inline zx_status_t usb_set_configuration(const usb_protocol_t* usb, int configuration) {
    return usb->ops->set_configuration(usb->ctx, configuration);
}

// Resets an endpoint that is in a halted or error state.
// Endpoints will be halted if the device returns a STALL in response to a USB transaction.
// When that occurs, the transaction will fail with ERR_IO_REFUSED.
// usb_reset_endpoint() the endpoint to normal running state.
static inline zx_status_t usb_reset_endpoint(const usb_protocol_t* usb, uint8_t ep_address) {
    return usb->ops->reset_endpoint(usb->ctx, ep_address);
}

// returns the maximum amount of data that can be transferred on an endpoint in a single transaction.
static inline size_t usb_get_max_transfer_size(const usb_protocol_t* usb, uint8_t ep_address) {
    return usb->ops->get_max_transfer_size(usb->ctx, ep_address);
}

static inline uint32_t usb_get_device_id(const usb_protocol_t* usb) {
    return usb->ops->get_device_id(usb->ctx);
}

static inline void usb_get_device_descriptor(const usb_protocol_t* usb,
                                             usb_device_descriptor_t* out_desc) {
    usb->ops->get_device_descriptor(usb->ctx, out_desc);
}

// returns the USB descriptors for the USB device or interface
// the returned value is de-allocated with free()
static inline zx_status_t usb_get_descriptor_list(const usb_protocol_t* usb, void** out_descriptors,
                                                  size_t* out_length) {
    return usb->ops->get_descriptor_list(usb->ctx, out_descriptors, out_length);
}

// returns the USB descriptors following the interface's existing descriptors
// the returned value is de-allocated with free()
static inline zx_status_t usb_get_additional_descriptor_list(const usb_protocol_t* usb,
                                                             void** out_descriptors,
                                                             size_t* out_length) {
    return usb->ops->get_additional_descriptor_list(usb->ctx, out_descriptors, out_length);
}

// Fetch the descriptor using the provided descriptor ID and language ID.  If
// the language ID requested is not available, the first entry of the language
// ID table will be used instead and be provided in the updated version of the
// parameter.
//
// The string will be encoded using UTF-8, and will be truncated to fit the
// space provided by the buflen parameter.  buflen will be updated to indicate
// the amount of space needed to hold the actual UTF-8 encoded string lenth, and
// may be larger than the original value passed.  Embedded nulls may be present
// in the string, and the result may not be null terminated if the string
// occupies the entire provided buffer.
//
static inline zx_status_t usb_get_string_descriptor(const usb_protocol_t* usb,
                                                    uint8_t desc_id, uint16_t* inout_lang_id,
                                                    uint8_t* buf, size_t* inout_buflen) {
    return usb->ops->get_string_descriptor(usb->ctx, desc_id, inout_lang_id, buf, inout_buflen);
}

// marks the interface as claimed and appends the interface descriptor to the
// interface's existing descriptors.
static inline zx_status_t usb_claim_interface(const usb_protocol_t* usb,
                                              usb_interface_descriptor_t* intf, size_t length) {
    return usb->ops->claim_interface(usb->ctx, intf, length);
}

static inline zx_status_t usb_cancel_all(const usb_protocol_t* usb, uint8_t ep_address) {
    return usb->ops->cancel_all(usb->ctx, ep_address);
}

__END_CDECLS;
