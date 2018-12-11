// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/phys-iter.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb-hub.h>
#include <zircon/listnode.h>

__BEGIN_CDECLS;

typedef struct usb_request usb_request_t;

// cache maintenance ops
#define USB_REQUEST_CACHE_INVALIDATE        ZX_VMO_OP_CACHE_INVALIDATE
#define USB_REQUEST_CACHE_CLEAN             ZX_VMO_OP_CACHE_CLEAN
#define USB_REQUEST_CACHE_CLEAN_INVALIDATE  ZX_VMO_OP_CACHE_CLEAN_INVALIDATE
#define USB_REQUEST_CACHE_SYNC              ZX_VMO_OP_CACHE_SYNC

typedef void (*usb_request_complete_cb)(usb_request_t* req, void* cookie);

// Returns a batch of completed requests for an endpoint.
// The client should free the completed_reqs array once they are finished with it.
typedef void (*usb_batch_complete_cb)(usb_request_t** completed_reqs, size_t num_completed,
                                      void* cookie);

// Should be set by the requester.
typedef struct usb_header {
    // frame number for scheduling isochronous transfers
    uint64_t frame;
    uint32_t device_id;
    // bEndpointAddress from endpoint descriptor
    uint8_t ep_address;
    // number of bytes to transfer
    zx_off_t length;
    // send zero length packet if length is multiple of max packet size
    bool send_zlp;
} usb_header_t;

// response data
// (filled in by processor before usb_request_complete() is called)
typedef struct usb_response {
    // status of transaction
    zx_status_t status;
    // number of bytes actually transferred (on success)
    zx_off_t actual;
} usb_response_t;

typedef struct usb_request {
    usb_header_t header;

    // for control transactions
    usb_setup_t setup;

    // vmo_handle for payload
    zx_handle_t vmo_handle;
    size_t size;
    // offset of the start of data from first page address of the vmo.
    zx_off_t offset;
    // mapped address of the first page of the vmo.
    // Add offset to get actual data.
    void* virt;

    zx_handle_t pmt;
    // phys addresses of the payload.
    zx_paddr_t* phys_list;
    // Number of physical pages of the payload.
    uint64_t phys_count;

    // Scatter gather entries of the payload.
    phys_iter_sg_entry_t* sg_list;
    // Number of entries in the scatter gather list.
    uint64_t sg_count;

    // The complete_cb() callback is set by the requester and is
    // invoked by the 'complete' ops method when it is called by
    // the processor upon completion of the usb request.
    // The saved_complete_cb field can be used to temporarily save
    // the original callback and overwrite it with the desired intermediate
    // callback.
    usb_request_complete_cb complete_cb;

    // Set by requester for passing data to complete_cb callback
    // The saved_cookie field can be used to temporarily save the
    // original cookie.
    void* cookie;

    // The current 'owner' of the usb request may save the original
    // complete callback and cookie, allowing them to insert an
    // intermediate callback.
    usb_request_complete_cb saved_complete_cb;
    void* saved_cookie;

    usb_response_t response;

    // The release_cb() callback is set by the allocator and is
    // invoked by the 'usb_request_release' method when it is called
    // by the requester.
    void (*release_cb)(usb_request_t* req);
    size_t alloc_size;

    // For requests queued on endpoints which have batching enabled via
    // usb_configure_batch_callback().
    // Set by the requester if a callback is required on this request's completion.
    // This is useful for isochronous requests, where the requester does not care about
    // most callbacks.
    // The requester should ensure the last request has this set to true.
    bool require_batch_cb;
} usb_request_t;


typedef struct {
    zx_status_t (*control)(void* ctx, uint8_t request_type, uint8_t request, uint16_t value,
                           uint16_t index, void* data, size_t length, zx_time_t timeout,
                           size_t* out_length);
    // queues a USB request
    void (*request_queue)(void* ctx, usb_request_t* usb_request, usb_request_complete_cb,
                          void* cookie);

    zx_status_t (*configure_batch_callback)(void* ctx, uint8_t ep_address,
                                            usb_batch_complete_cb cb, void* cookie);

    usb_speed_t (*get_speed)(void* ctx);
    zx_status_t (*set_interface)(void* ctx, uint8_t interface_number, uint8_t alt_setting);
    uint8_t (*get_configuration)(void* ctx);
    zx_status_t (*set_configuration)(void* ctx, uint8_t configuration);
    zx_status_t (*enable_endpoint)(void* ctx, usb_endpoint_descriptor_t* ep_desc,
                                   usb_ss_ep_comp_descriptor_t* ss_comp_desc, bool enable);
    zx_status_t (*reset_endpoint)(void* ctx, uint8_t ep_address);
    size_t (*get_max_transfer_size)(void* ctx, uint8_t ep_address);
    uint32_t (*get_device_id)(void* ctx);
    void (*get_device_descriptor)(void* ctx, usb_device_descriptor_t* out_desc);
    zx_status_t (*get_configuration_descriptor)(void* ctx, uint8_t configuration,
                                                usb_configuration_descriptor_t** out,
                                                size_t* out_length);
    zx_status_t (*get_descriptor_list)(void* ctx, void** out_descriptors, size_t* out_length);
    zx_status_t (*get_string_descriptor)(void* ctx, uint8_t desc_id, uint16_t lang_id,
                                         uint8_t* buf, size_t buflen, size_t* out_actual,
                                         uint16_t* out_actual_lang_id);
    zx_status_t (*cancel_all)(void* ctx, uint8_t ep_address);
    uint64_t (*get_current_frame)(void* ctx);
    size_t (*get_request_size)(void* ctx);
} usb_protocol_ops_t;

typedef struct usb_protocol {
    usb_protocol_ops_t* ops;
    void* ctx;
} usb_protocol_t;

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

static inline void usb_request_queue(const usb_protocol_t* usb, usb_request_t* usb_request,
                                     usb_request_complete_cb cb, void* cookie) {
    return usb->ops->request_queue(usb->ctx, usb_request, cb, cookie);
}

// Configures an endpoint to batch multiple requests to a single callback.
// Requests will receive a callback if they have set require_batch_cb to true, or an error occurs.
//   ep_address: the endpoint which requests will be queued on.
//   complete_cb: callback for the batch of completed requests.
//   cookie: user data passed to the |complete_cb|.
static inline zx_status_t usb_configure_batch_callback(const usb_protocol_t* usb,
                                                       uint8_t ep_address,
                                                       usb_batch_complete_cb complete_cb,
                                                       void* cookie) {
    return usb->ops->configure_batch_callback(usb->ctx, ep_address, complete_cb, cookie);
}

static inline usb_speed_t usb_get_speed(const usb_protocol_t* usb) {
    return usb->ops->get_speed(usb->ctx);
}

static inline zx_status_t usb_set_interface(const usb_protocol_t* usb, uint8_t interface_number,
                                            uint8_t alt_setting) {
    return usb->ops->set_interface(usb->ctx, interface_number, alt_setting);
}

static inline uint8_t usb_get_configuration(const usb_protocol_t* usb) {
    return usb->ops->get_configuration(usb->ctx);
}

static inline zx_status_t usb_set_configuration(const usb_protocol_t* usb, uint8_t configuration) {
    return usb->ops->set_configuration(usb->ctx, configuration);
}

static inline zx_status_t usb_enable_endpoint(const usb_protocol_t* usb,
                                              usb_endpoint_descriptor_t* ep_desc,
                                              usb_ss_ep_comp_descriptor_t* ss_comp_desc,
                                              bool enable) {
    return usb->ops->enable_endpoint(usb->ctx, ep_desc, ss_comp_desc, enable);
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

// Returns the device ID for the device.
// This ID is generated by and used internally by the USB HCI controller driver.
static inline uint32_t usb_get_device_id(const usb_protocol_t* usb) {
    return usb->ops->get_device_id(usb->ctx);
}

// Returns the device's device descriptor.
static inline void usb_get_device_descriptor(const usb_protocol_t* usb,
                                             usb_device_descriptor_t* out_desc) {
    usb->ops->get_device_descriptor(usb->ctx, out_desc);
}

// Returns the configuration descriptor for the given configuration.
static inline zx_status_t usb_get_configuration_descriptor(const usb_protocol_t* usb,
                                                           uint8_t configuration,
                                                           usb_configuration_descriptor_t** out,
                                                           size_t* out_length) {
    return usb->ops->get_configuration_descriptor(usb->ctx, configuration, out, out_length);
}

// returns the USB descriptors for the USB device or interface
// the returned value is de-allocated with free()
static inline zx_status_t usb_get_descriptor_list(const usb_protocol_t* usb, void** out_descriptors,
                                                  size_t* out_length) {
    return usb->ops->get_descriptor_list(usb->ctx, out_descriptors, out_length);
}

// Fetch the descriptor using the provided descriptor ID and language ID.  If
// the language ID requested is not available, the first entry of the language
// ID table will be used instead and be provided in the updated version of the
// parameter.
//
// The string will be encoded using UTF-8, and will be truncated to fit the
// space provided by the buflen parameter.  Embedded nulls may be present
// in the string, and the result may not be null terminated if the string
// occupies the entire provided buffer.
//
static inline zx_status_t usb_get_string_descriptor(const usb_protocol_t* usb, uint8_t desc_id, 
                                                    uint16_t lang_id, uint8_t* buf, size_t buflen,
                                                    size_t* out_actual,
                                                    uint16_t* out_actual_lang_id) {
    return usb->ops->get_string_descriptor(usb->ctx, desc_id, lang_id, buf, buflen, out_actual,
                                           out_actual_lang_id);
}

static inline zx_status_t usb_cancel_all(const usb_protocol_t* usb, uint8_t ep_address) {
    return usb->ops->cancel_all(usb->ctx, ep_address);
}

// returns the current frame (in milliseconds), used for isochronous transfers
static inline uint64_t usb_get_current_frame(const usb_protocol_t* usb) {
    return usb->ops->get_current_frame(usb->ctx);
}

// return the internal context size plus parents request size
static inline uint64_t usb_get_request_size(const usb_protocol_t* usb) {
    return usb->ops->get_request_size(usb->ctx);
}
__END_CDECLS;
