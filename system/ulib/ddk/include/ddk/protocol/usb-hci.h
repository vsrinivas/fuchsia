// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/usb-request.h>
#include <ddk/protocol/usb.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb-hub.h>
#include <stdbool.h>

__BEGIN_CDECLS;

typedef struct usb_bus_interface usb_bus_interface_t;

typedef struct usb_hci_protocol_ops {
    // queues a USB request
    void (*request_queue)(void* ctx, usb_request_t* usb_request);
    void (*set_bus_interface)(void* ctx, usb_bus_interface_t* bus_intf);
    size_t (*get_max_device_count)(void* ctx);
    // enables or disables an endpoint using parameters derived from ep_desc
    zx_status_t (*enable_endpoint)(void* ctx, uint32_t device_id,
                                   usb_endpoint_descriptor_t* ep_desc,
                                   usb_ss_ep_comp_descriptor_t* ss_comp_desc, bool enable);

    // returns the current frame (in milliseconds), used for isochronous transfers
    uint64_t (*get_current_frame)(void* ctx);

    // Hub support
    zx_status_t (*configure_hub)(void* ctx, uint32_t device_id, usb_speed_t speed,
                 usb_hub_descriptor_t* descriptor);
    zx_status_t (*hub_device_added)(void* ctx, uint32_t device_id, int port, usb_speed_t speed);
    zx_status_t (*hub_device_removed)(void* ctx, uint32_t device_id, int port);
    zx_status_t (*reset_endpoint)(void* ctx, uint32_t device_id, uint8_t ep_address);
    size_t (*get_max_transfer_size)(void* ctx, uint32_t device_id, uint8_t ep_address);
    zx_status_t (*cancel_all)(void* ctx, uint32_t device_id, uint8_t ep_address);
    zx_status_t (*get_bti)(void* ctx, zx_handle_t* out_handle);
} usb_hci_protocol_ops_t;

typedef struct usb_hci_protocol {
    usb_hci_protocol_ops_t* ops;
    void* ctx;
} usb_hci_protocol_t;

static inline void usb_hci_request_queue(usb_hci_protocol_t* hci, usb_request_t* usb_request) {
    return hci->ops->request_queue(hci->ctx, usb_request);
}

static inline void usb_hci_set_bus_interface(usb_hci_protocol_t* hci, usb_bus_interface_t* intf) {
    hci->ops->set_bus_interface(hci->ctx, intf);
}

static inline size_t usb_hci_get_max_device_count(usb_hci_protocol_t* hci) {
    return hci->ops->get_max_device_count(hci->ctx);

}

// enables or disables an endpoint using parameters derived from ep_desc
static inline zx_status_t usb_hci_enable_endpoint(usb_hci_protocol_t* hci, uint32_t device_id,
                                   usb_endpoint_descriptor_t* ep_desc,
                                   usb_ss_ep_comp_descriptor_t* ss_comp_desc, bool enable) {
    return hci->ops->enable_endpoint(hci->ctx, device_id, ep_desc, ss_comp_desc, enable);
}

// returns the current frame (in milliseconds), used for isochronous transfers
static inline uint64_t usb_hci_get_current_frame(usb_hci_protocol_t* hci) {
    return hci->ops->get_current_frame(hci->ctx);
}

static inline zx_status_t usb_hci_configure_hub(usb_hci_protocol_t* hci, uint32_t device_id,
                                                usb_speed_t speed,
                                                usb_hub_descriptor_t* descriptor) {
    return hci->ops->configure_hub(hci->ctx, device_id, speed, descriptor);
}

static inline zx_status_t usb_hci_hub_device_added(usb_hci_protocol_t* hci, uint32_t device_id,
                                                   int port, usb_speed_t speed) {
    return hci->ops->hub_device_added(hci->ctx, device_id, port, speed);
}

static inline zx_status_t usb_hci_hub_device_removed(usb_hci_protocol_t* hci, uint32_t device_id,
                                                     int port) {
    return hci->ops->hub_device_removed(hci->ctx, device_id, port);
}

static inline zx_status_t usb_hci_reset_endpoint(usb_hci_protocol_t* hci, uint32_t device_id,
                                                 uint8_t ep_address) {
    return hci->ops->reset_endpoint(hci->ctx, device_id, ep_address);
}

static inline size_t usb_hci_get_max_transfer_size(usb_hci_protocol_t* hci, uint32_t device_id,
                                                   uint8_t ep_address) {
    return hci->ops->get_max_transfer_size(hci->ctx, device_id, ep_address);
}

static inline zx_status_t usb_hci_cancel_all(usb_hci_protocol_t* hci, uint32_t device_id,
                                             uint8_t ep_address) {
    return hci->ops->cancel_all(hci->ctx, device_id, ep_address);
}

// shares a copy of the HCI driver's BTI handle
static inline zx_status_t usb_hci_get_bti(usb_hci_protocol_t* hci, zx_handle_t* out_handle) {
    return hci->ops->get_bti(hci->ctx, out_handle);
}

__END_CDECLS;
