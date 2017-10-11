// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/usb.h>
#include <ddk/usb-request.h>
#include <zircon/syscalls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

// Frees any resources allocated by the usb request, but not the usb request itself.
static void usb_request_release_static(usb_request_t* req) {
    io_buffer_release(&req->buffer);
}

// Frees any resources allocated by the usb request, as well as the usb request itself.
static void usb_request_release_free(usb_request_t* req) {
    usb_request_release_static(req);
    free(req);
}

zx_status_t usb_request_alloc(usb_request_t** out, uint64_t data_size, uint8_t ep_address) {
    usb_request_t* req = calloc(1, sizeof(usb_request_t));
    if (!req) {
        return ZX_ERR_NO_MEMORY;
    }
    if (data_size > 0) {
        zx_status_t status = io_buffer_init(&req->buffer, data_size, IO_BUFFER_RW);
        if (status != ZX_OK) {
            free(req);
            return status;
        }
    }
    req->header.ep_address = ep_address;
    req->header.length = data_size;
    req->release_cb = usb_request_release_free;
    *out = req;
    return ZX_OK;
}

zx_status_t usb_request_alloc_vmo(usb_request_t** out, zx_handle_t vmo_handle,
                                  uint64_t vmo_offset, uint64_t length, uint8_t ep_address) {
    usb_request_t* req = calloc(1, sizeof(usb_request_t));
    if (!req) {
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t status = io_buffer_init_vmo(&req->buffer, vmo_handle, vmo_offset, IO_BUFFER_RW);
    if (status != ZX_OK) {
        free(req);
        return status;
    }
    req->header.ep_address = ep_address;
    req->header.length = length;
    req->release_cb = usb_request_release_free;
    *out = req;
    return ZX_OK;
}

zx_status_t usb_request_init(usb_request_t* req, zx_handle_t vmo_handle, uint64_t vmo_offset,
                             uint64_t length, uint8_t ep_address) {
    memset(req, 0, sizeof(*req));

    zx_status_t status = io_buffer_init_vmo(&req->buffer, vmo_handle, vmo_offset, IO_BUFFER_RW);
    if (status != ZX_OK) {
        return status;
    }
    req->header.ep_address = ep_address;
    req->header.length = length;
    req->release_cb = usb_request_release_static;
    return ZX_OK;
}

ssize_t usb_request_copyfrom(usb_request_t* req, void* data, size_t length, size_t offset) {
    length = MIN(req->buffer.size - req->buffer.offset - offset, length);
    memcpy(data, io_buffer_virt(&req->buffer) + offset, length);
    return length;
}

ssize_t usb_request_copyto(usb_request_t* req, const void* data, size_t length, size_t offset) {
    length = MIN(req->buffer.size - req->buffer.offset - offset, length);
    memcpy(io_buffer_virt(&req->buffer) + offset, data, length);
    return length;
}

zx_status_t usb_request_mmap(usb_request_t* req, void** data) {
    *data = io_buffer_virt(&req->buffer);
    // TODO(jocelyndang): modify this once we start passing usb requests across process boundaries.
    return ZX_OK;
}

zx_status_t usb_request_physmap(usb_request_t* req) {
    return io_buffer_physmap(&req->buffer);
}

void usb_request_release(usb_request_t* req) {
    if (req->release_cb) {
        req->release_cb(req);
    }
}

void usb_request_complete(usb_request_t* req, zx_status_t status, zx_off_t actual) {
    req->response.status = status;
    req->response.actual = actual;

    if (req->complete_cb) {
        req->complete_cb(req, req->cookie);
    }
}

void usb_request_phys_iter_init(phys_iter_t* iter, usb_request_t* req, size_t max_length) {
    phys_iter_buffer_t buf = {
        .length = req->header.length,
        .vmo_offset = req->buffer.offset,
        .phys = req->buffer.phys_list,
        .phys_count = req->buffer.phys_count
    };
    phys_iter_init(iter, &buf, max_length);
}

size_t usb_request_phys_iter_next(phys_iter_t* iter, zx_paddr_t* out_paddr) {
    return phys_iter_next(iter, out_paddr);
}

// Helper functions for converting a usb request to an iotxn.
// TODO(jocelyndang): remove once all usb drivers have transitioned to usb requests.

// Completion callback for iotxns converted from usb requests.
static void converted_iotxn_complete(iotxn_t* txn, void* cookie) {
    usb_request_t* req = cookie;

    usb_request_complete(req, txn->status, txn->actual);

    iotxn_release(txn);
}

zx_status_t usb_request_to_iotxn(usb_request_t* req, iotxn_t** out) {
    iotxn_t* txn;
    zx_status_t status = iotxn_alloc_vmo(&txn, 0, req->buffer.vmo_handle,
                                         req->buffer.offset, req->header.length);
    if (status != ZX_OK) {
        return status;
    }

    usb_protocol_data_t* data = iotxn_pdata(txn, usb_protocol_data_t);

    data->setup = req->setup;
    data->frame = req->header.frame;
    data->device_id = req->header.device_id;
    data->ep_address = req->header.ep_address;
    txn->length = req->header.length;
    txn->protocol = ZX_PROTOCOL_USB;

    txn->complete_cb = converted_iotxn_complete;
    txn->cookie = req;

    *out = txn;
    return status;
}
