// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/usb.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/hw/usb.h>

__BEGIN_CDECLS;

// This protocol is used for USB peripheral function functions

// callbacks implemented by the function driver
typedef struct {

    // return the descriptor list for the function
    // TODO(voydanoff) - descriptors will likely vary (different max packet sizes, etc)
    // depending on whether we are in low/full, high or super speed mode.
    // We will need to add a usb_speed_t argument to this callback.
    const usb_descriptor_header_t* (*get_descriptors)(void* ctx, size_t* out_length);

    // callback for handling ep0 control requests
    zx_status_t (*control)(void* ctx, const usb_setup_t* setup, void* buffer, size_t buffer_length,
                           size_t* out_actual);

    // Called to inform the function driver when the USB device configured state changes.
    // Called with configured == true in response to a SET_CONFIGURATION control request
    // that selects a configuration that contains this function. In this case, the function driver
    // should call usb_function_config_ep() to configure its endpoints.
    // Called with configured == false when configuration is disabled or USB is disconnected.
    // The function driver should then call usb_function_disable_ep() to disable its endpoints.
    zx_status_t (*set_configured)(void* ctx, bool configured, usb_speed_t speed);

    // Called to set an alternate setting for an interface due to a SET_INTERFACE control request.
    // The function driver should call usb_function_config_ep() and/or usb_function_config_ep()
    // to configure or disable the interface's endpoints as appropriate.
    zx_status_t (*set_interface)(void* ctx, unsigned interface, unsigned alt_setting);
} usb_function_interface_ops_t;

typedef struct {
    usb_function_interface_ops_t* ops;
    void* ctx;
} usb_function_interface_t;

static inline const usb_descriptor_header_t* usb_function_get_descriptors(
                                        usb_function_interface_t* intf, size_t* out_length) {
    return intf->ops->get_descriptors(intf->ctx, out_length);
}

static inline zx_status_t usb_function_control(usb_function_interface_t* intf,
                                               const usb_setup_t* setup, void* buffer,
                                               size_t buffer_length, size_t* out_actual) {
    return intf->ops->control(intf->ctx, setup, buffer, buffer_length, out_actual);
}

static inline zx_status_t usb_function_set_configured(usb_function_interface_t* intf,
                                                      bool configured, usb_speed_t speed) {
    return intf->ops->set_configured(intf->ctx, configured, speed);
}

static inline zx_status_t usb_function_set_interface(usb_function_interface_t* intf,
                                                     unsigned interface, unsigned alt_setting) {
    return intf->ops->set_interface(intf->ctx, interface, alt_setting);
}

typedef struct {
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
    zx_status_t (*register_func)(void* ctx, usb_function_interface_t* intf);
    zx_status_t (*alloc_interface)(void* ctx, uint8_t* out_intf_num);
    zx_status_t (*alloc_ep)(void* ctx, uint8_t direction, uint8_t* out_address);
    zx_status_t (*config_ep)(void* ctx, usb_endpoint_descriptor_t* ep_desc,
                             usb_ss_ep_comp_descriptor_t* ss_comp_desc);
    zx_status_t (*disable_ep)(void* ctx, uint8_t ep_addr);
    zx_status_t (*alloc_string_desc)(void* ctx, const char* string, uint8_t* out_index);
    void (*queue)(void* ctx, usb_request_t* req);
    zx_status_t (*ep_set_stall)(void* ctx, uint8_t ep_address);
    zx_status_t (*ep_clear_stall)(void* ctx, uint8_t ep_address);
} usb_function_protocol_ops_t;

typedef struct {
    usb_function_protocol_ops_t* ops;
    void* ctx;
} usb_function_protocol_t;

static inline zx_status_t usb_function_req_alloc(usb_function_protocol_t* usb, usb_request_t** out,
                                                 uint64_t data_size, uint8_t ep_address) {
    return usb->ops->req_alloc(usb->ctx, out, data_size, ep_address);
}

static inline zx_status_t usb_function_req_alloc_vmo(usb_function_protocol_t* usb,
                                                     usb_request_t** out, zx_handle_t vmo_handle,
                                                     uint64_t vmo_offset, uint64_t length,
                                                     uint8_t ep_address) {
    return usb->ops->req_alloc_vmo(usb->ctx, out, vmo_handle, vmo_offset, length, ep_address);
}

static inline zx_status_t usb_function_req_init(usb_function_protocol_t* usb, usb_request_t* req,
                                                zx_handle_t vmo_handle, uint64_t vmo_offset,
                                                uint64_t length, uint8_t ep_address) {
    return usb->ops->req_init(usb->ctx, req, vmo_handle, vmo_offset, length, ep_address);
}

static inline ssize_t usb_function_req_copy_from(usb_function_protocol_t* usb, usb_request_t* req,
                                                 void* data, size_t length, size_t offset) {
    return usb->ops->req_copy_from(usb->ctx, req, data, length, offset);
}

static inline ssize_t usb_function_req_copy_to(usb_function_protocol_t* usb, usb_request_t* req,
                                               const void* data, size_t length, size_t offset) {
    return usb->ops->req_copy_to(usb->ctx, req, data, length, offset);
}

static inline zx_status_t usb_function_req_mmap(usb_function_protocol_t* usb, usb_request_t* req,
                                                void** data) {
    return usb->ops->req_mmap(usb->ctx, req, data);
}

static inline zx_status_t usb_function_req_cacheop(usb_function_protocol_t* usb, usb_request_t* req,
                                                   uint32_t op, size_t offset, size_t length) {
    return usb->ops->req_cacheop(usb->ctx, req, op, offset, length);
}

static inline zx_status_t usb_function_req_cache_flush(usb_function_protocol_t* usb,
                                                       usb_request_t* req, size_t offset,
                                                       size_t length) {
    return usb->ops->req_cache_flush(usb->ctx, req, offset, length);
}

static inline zx_status_t usb_function_req_cache_flush_invalidate(usb_function_protocol_t* usb,
                                                                  usb_request_t* req,
                                                                  zx_off_t offset,
                                                                  size_t length) {
    return usb->ops->req_cache_flush_invalidate(usb->ctx, req, offset, length);
}

static inline zx_status_t usb_fucntion_req_physmap(usb_function_protocol_t* usb, usb_request_t* req) {
    return usb->ops->req_physmap(usb->ctx, req);
}

static inline void usb_function_req_release(usb_function_protocol_t* usb, usb_request_t* req) {
    usb->ops->req_release(usb->ctx, req);
}

static inline void usb_function_req_complete(usb_function_protocol_t* usb, usb_request_t* req,
                                             zx_status_t status, zx_off_t actual) {
    usb->ops->req_complete(usb->ctx, req, status, actual);
}

static inline void usb_function_req_phys_iter_init(usb_function_protocol_t* usb, phys_iter_t* iter,
                                                   usb_request_t* req, size_t max_length) {
    usb->ops->req_phys_iter_init(usb->ctx, iter, req, max_length);
}

// registers the function driver's callback interface
static inline void usb_function_register(usb_function_protocol_t* func,
                                         usb_function_interface_t* intf) {
    func->ops->register_func(func->ctx, intf);
}

// allocates a unique interface descriptor number
static inline zx_status_t usb_function_alloc_interface(usb_function_protocol_t* func,
                                                       uint8_t* out_intf_num) {
    return func->ops->alloc_interface(func->ctx, out_intf_num);
}

// allocates a unique endpoint descriptor number
// direction should be either USB_DIR_OUT or USB_DIR_IN
static inline zx_status_t usb_function_alloc_ep(usb_function_protocol_t* func, uint8_t direction,
                                                uint8_t* out_address) {
    return func->ops->alloc_ep(func->ctx, direction, out_address);
}

// configures an endpoint based on the provided usb_endpoint_descriptor_t and
// usb_ss_ep_comp_descriptor_t descriptors.
static inline zx_status_t usb_function_config_ep(usb_function_protocol_t* func,
                                                 usb_endpoint_descriptor_t* ep_desc,
                                                 usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    return func->ops->config_ep(func->ctx, ep_desc, ss_comp_desc);
}

// disables an endpoint. called when the device is no longer configured or an alternate interface
// is selected.
static inline zx_status_t usb_function_disable_ep(usb_function_protocol_t* func, uint8_t ep_addr) {
    return func->ops->disable_ep(func->ctx, ep_addr);
}

// adds a string descriptor to the device configuration.
static inline zx_status_t usb_function_alloc_string_desc(usb_function_protocol_t* func,
                                                         const char* string, uint8_t* out_index) {
    return func->ops->alloc_string_desc(func->ctx, string, out_index);
}

// helper for queueing a usb request on an endpoint.
static inline void usb_function_queue(usb_function_protocol_t* func, usb_request_t* req) {
    return func->ops->queue(func->ctx, req);
}

// stalls an endpoint
static zx_status_t usb_function_ep_set_stall(usb_function_protocol_t* func, uint8_t ep_address) {
    return func->ops->ep_set_stall(func->ctx, ep_address);
}

// clears endpoint stall state
static zx_status_t usb_function_ep_clear_stall(usb_function_protocol_t* func, uint8_t ep_address) {
    return func->ops->ep_clear_stall(func->ctx, ep_address);
}

__END_CDECLS;
