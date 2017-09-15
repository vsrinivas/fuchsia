
// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/iotxn.h>
#include <zircon/hw/usb.h>
#include <zircon/listnode.h>

__BEGIN_CDECLS;

typedef struct usb_request usb_request_t;

// Should be set by the requestor.
typedef struct usb_header {
    // frame number for scheduling isochronous transfers
    uint64_t frame;
    uint32_t device_id;
    // bEndpointAddress from endpoint descriptor
    uint8_t ep_address;
    // number of bytes to transfer
    zx_off_t length;
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

    // request payload
    io_buffer_t buffer;

    // The complete_cb() callback is set by the requestor and is
    // invoked by the 'complete' ops method when it is called by
    // the processor upon completion of the usb request.
    void (*complete_cb)(usb_request_t* req, void* cookie);
    // Set by requestor for passing data to complete_cb callback
    // May not be modified by anyone other than the requestor.
    void* cookie;

    usb_response_t response;

    // list node and context
    // the current "owner" of the usb_request may use these however desired
    // (eg, the requestor may use node to hold the usb_request on a free list
    // and when it's queued the processor may use node to hold the usb_request
    // in a transaction queue)
    list_node_t node;
} usb_request_t;

// usb_request_alloc() creates a new usb request with payload space of data_size.
zx_status_t usb_request_alloc(usb_request_t** out, uint64_t data_size, uint8_t ep_address);

// usb_request_alloc_vmo() creates a new usb request with the given vmo.
zx_status_t usb_request_alloc_vmo(usb_request_t** out, zx_handle_t vmo_handle,
                                  uint64_t vmo_offset, uint64_t length, uint8_t ep_address);

// usb_request_copyfrom() copies data from the usb_request's vm object.
// Out of range operations are ignored.
ssize_t usb_request_copyfrom(usb_request_t* req, void* data, size_t length, size_t offset);

// usb_request_copyto() copies data into a usb_request's vm object.
// Out of range operations are ignored.
ssize_t usb_request_copyto(usb_request_t* req, const void* data, size_t length, size_t offset);

// usb_request_virt() returns the virtual address of the vm object.
void* usb_request_virt(usb_request_t* req);

// usb_request_release() frees the message data -- should be called only by the entity that allocated it
void usb_request_release(usb_request_t* req);

// usb_request_complete() must be called by the processor when the request has
// completed or failed and the request and any virtual or physical memory obtained
// from it may not be touched again by the processor.
//
// The usb_request's complete_cb() will be called as the last action of
// this method.
void usb_request_complete(usb_request_t* req, zx_status_t status, zx_off_t actual);

// usb_request_to_iotxn() converts a USB request to an iotxn.
// The original USB request is not freed.
zx_status_t usb_request_to_iotxn(usb_request_t* req, iotxn_t** out);

__END_CDECLS;
