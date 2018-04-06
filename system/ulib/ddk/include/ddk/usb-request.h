
// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <sys/types.h>
#include <ddk/io-buffer.h>
#include <ddk/phys-iter.h>
#include <zircon/hw/usb.h>
#include <zircon/listnode.h>

#include <threads.h>

__BEGIN_CDECLS;

typedef struct usb_request usb_request_t;

// cache maintenance ops
#define USB_REQUEST_CACHE_INVALIDATE        ZX_VMO_OP_CACHE_INVALIDATE
#define USB_REQUEST_CACHE_CLEAN             ZX_VMO_OP_CACHE_CLEAN
#define USB_REQUEST_CACHE_CLEAN_INVALIDATE  ZX_VMO_OP_CACHE_CLEAN_INVALIDATE
#define USB_REQUEST_CACHE_SYNC              ZX_VMO_OP_CACHE_SYNC

typedef void (*usb_request_complete_cb)(usb_request_t* req, void* cookie);

// Should be set by the requestor.
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

    // request payload
    io_buffer_t buffer;

    // The complete_cb() callback is set by the requestor and is
    // invoked by the 'complete' ops method when it is called by
    // the processor upon completion of the usb request.
    // The saved_complete_cb field can be used to temporarily save
    // the original callback and overwrite it with the desired intermediate
    // callback.
    usb_request_complete_cb complete_cb;

    // Set by requestor for passing data to complete_cb callback
    // The saved_cookie field can be used to temporarily save the
    // original cookie.
    void* cookie;

    // The current 'owner' of the usb request may save the original
    // complete callback and cookie, allowing them to insert an
    // intermediate callback.
    usb_request_complete_cb saved_complete_cb;
    void* saved_cookie;

    usb_response_t response;

    // list node and context
    // the current "owner" of the usb_request may use these however desired
    // (eg, the requestor may use node to hold the usb_request on a free list
    // and when it's queued the processor may use node to hold the usb_request
    // in a transaction queue)
    list_node_t node;

    void *context;

    // The release_cb() callback is set by the allocator and is
    // invoked by the 'usb_request_release' method when it is called
    // by the requestor.
    void (*release_cb)(usb_request_t* req);
} usb_request_t;

typedef struct {
    list_node_t free_reqs;
    mtx_t lock;
} usb_request_pool_t;

// usb_request_alloc() creates a new usb request with payload space of data_size.
zx_status_t usb_request_alloc(usb_request_t** out, zx_handle_t bti_handle, uint64_t data_size,
                              uint8_t ep_address);

// usb_request_alloc_vmo() creates a new usb request with the given VMO.
zx_status_t usb_request_alloc_vmo(usb_request_t** out, zx_handle_t bti_handle,
                                  zx_handle_t vmo_handle, uint64_t vmo_offset, uint64_t length,
                                  uint8_t ep_address);

// usb_request_init() initializes the statically allocated usb request with the given VMO.
// This will free any resources allocated by the usb request but not the usb request itself.
zx_status_t usb_request_init(usb_request_t* req, zx_handle_t bti_handle, zx_handle_t vmo_handle,
                             uint64_t vmo_offset, uint64_t length, uint8_t ep_address);

// usb_request_copyfrom() copies data from the usb_request's vm object.
// Out of range operations are ignored.
ssize_t usb_request_copyfrom(usb_request_t* req, void* data, size_t length, size_t offset);

// usb_request_copyto() copies data into a usb_request's vm object.
// Out of range operations are ignored.
ssize_t usb_request_copyto(usb_request_t* req, const void* data, size_t length, size_t offset);

// usb_request_mmap() maps the usb request's vm object. The 'data' field is set with the
// mapped address if this function succeeds.
zx_status_t usb_request_mmap(usb_request_t* req, void** data);

// usb_request_cacheop() performs a cache maintenance op against the request's internal
// buffer.
zx_status_t usb_request_cacheop(usb_request_t* req, uint32_t op, size_t offset, size_t length);

// usb_request_cache_flush() performs a cache flush on a range of memory in the request's buffer
zx_status_t usb_request_cache_flush(usb_request_t* req, zx_off_t offset, size_t length);

// usb_request_cache_flush_invalidate() performs a cache flush and invalidate on a range of memory
// in the request's buffer
zx_status_t usb_request_cache_flush_invalidate(usb_request_t* req, zx_off_t offset, size_t length);

// Looks up the physical pages backing this request's vm object.
zx_status_t usb_request_physmap(usb_request_t* req);

// usb_request_release() frees the message data -- should be called only by the entity that allocated it
void usb_request_release(usb_request_t* req);

// usb_request_complete() must be called by the processor when the request has
// completed or failed and the request and any virtual or physical memory obtained
// from it may not be touched again by the processor.
//
// The usb_request's complete_cb() will be called as the last action of
// this method.
void usb_request_complete(usb_request_t* req, zx_status_t status, zx_off_t actual);

// initializes a phys_iter_t for a usb request
// max_length is the maximum length of a range returned by usb_request_phys_iter_next()
// max_length must be either a positive multiple of PAGE_SIZE, or zero for no limit.
void usb_request_phys_iter_init(phys_iter_t* iter, usb_request_t* req, size_t max_length);

// returns the next physical address and length for the iterator up to size max_length.
// return value is length, or zero if iteration is done.
size_t usb_request_phys_iter_next(phys_iter_t* iter, zx_paddr_t* out_paddr);

// usb_request_pool_init() initializes the given pool. A driver may use
// a pool for recycling their own usb requests.
void usb_request_pool_init(usb_request_pool_t* pool);

// usb_request_pool_add() adds the request to the pool.
void usb_request_pool_add(usb_request_pool_t* pool, usb_request_t* req);

// returns a request from the pool that has a buffer of the given length,
// or null if no such request exists.
// The request is not re-initialized in any way and should be set accordingly by the user.
usb_request_t* usb_request_pool_get(usb_request_pool_t* pool, size_t length);

__END_CDECLS;
