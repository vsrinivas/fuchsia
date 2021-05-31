// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_USB_REQUEST_H_
#define SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_USB_REQUEST_H_

#include <fuchsia/hardware/usb/request/c/banjo.h>
#include <lib/ddk/io-buffer.h>
#include <lib/ddk/phys-iter.h>
#include <stdint.h>
#include <sys/types.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/hw/usb.h>
#include <zircon/listnode.h>

__BEGIN_CDECLS

// cache maintenance ops
// clang-format off
#define USB_REQUEST_CACHE_INVALIDATE        ZX_VMO_OP_CACHE_INVALIDATE
#define USB_REQUEST_CACHE_CLEAN             ZX_VMO_OP_CACHE_CLEAN
#define USB_REQUEST_CACHE_CLEAN_INVALIDATE  ZX_VMO_OP_CACHE_CLEAN_INVALIDATE
#define USB_REQUEST_CACHE_SYNC              ZX_VMO_OP_CACHE_SYNC
// clang-format on

typedef struct {
  list_node_t free_reqs;
  mtx_t lock;
  // offset of the list_node_t* (used for queueing) in usb_request_t.
  uint64_t node_offset;
} usb_request_pool_t;

typedef struct {
  list_node_t node;
} usb_req_internal_t;

#define USB_REQ_TO_REQ_INTERNAL(req, size) ((usb_req_internal_t*)((uintptr_t)(req) + (size)))
#define REQ_INTERNAL_TO_USB_REQ(ctx, size) ((usb_request_t*)((uintptr_t)(ctx) - (size)))

// usb_request_alloc() creates a new usb request with payload space of data_size.
zx_status_t usb_request_alloc(usb_request_t** out, uint64_t data_size, uint8_t ep_address,
                              size_t req_size);

// usb_request_alloc_vmo() creates a new usb request with the given VMO.
zx_status_t usb_request_alloc_vmo(usb_request_t** out, zx_handle_t vmo_handle, uint64_t vmo_offset,
                                  uint64_t length, uint8_t ep_address, size_t req_size);

// usb_request_init() initializes the statically allocated usb request with the given VMO.
// This will free any resources allocated by the usb request but not the usb request itself.
zx_status_t usb_request_init(usb_request_t* req, zx_handle_t vmo_handle, uint64_t vmo_offset,
                             uint64_t length, uint8_t ep_address);

// usb_request_set_sg_list() copies the scatter gather list to the request.
// Future transfers using this request will determine where in the VMO to store read / write data
// using the scatter gather list.
// This will free any existing scatter gather list stored in the request.
zx_status_t usb_request_set_sg_list(usb_request_t* req, const sg_entry_t* sg_list, size_t sg_count);

// usb_request_copy_from() copies data from the usb_request's vm object.
// Out of range operations are ignored.
__WARN_UNUSED_RESULT ssize_t usb_request_copy_from(usb_request_t* req, void* data, size_t length,
                                                   size_t offset);

// usb_request_copy_to() copies data into a usb_request's vm object.
// Out of range operations are ignored.
__WARN_UNUSED_RESULT ssize_t usb_request_copy_to(usb_request_t* req, const void* data,
                                                 size_t length, size_t offset);

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
zx_status_t usb_request_physmap(usb_request_t* req, zx_handle_t bti_handle);

// usb_request_release() frees the message data -- should be called only by the entity that
// allocated it
void usb_request_release(usb_request_t* req);

// usb_request_complete() must be called by the processor when the request has
// completed or failed and the request and any virtual or physical memory obtained
// from it may not be touched again by the processor.
//
// The complete_cb() will be called as the last action of this method.
void usb_request_complete(usb_request_t* req, zx_status_t status, zx_off_t actual,
                          const usb_request_complete_callback_t* complete_cb);

// Same as usb_request_complete() but also allows specifying the number of
// silently completed requests.
void usb_request_complete_base(usb_request_t* req, zx_status_t status, zx_off_t actual,
                               size_t silent_completions_count,
                               const usb_request_complete_callback_t* complete_cb);

// initializes a phys_iter_t for a usb request
// max_length is the maximum length of a range returned by usb_request_phys_iter_next()
// max_length must be either a positive multiple of the system page size, or zero for no limit.
void usb_request_phys_iter_init(phys_iter_t* iter, usb_request_t* req, size_t max_length);

// returns the next physical address and length for the iterator up to size max_length.
// return value is length, or zero if iteration is done.
size_t usb_request_phys_iter_next(phys_iter_t* iter, zx_paddr_t* out_paddr);

// usb_request_pool_init() initializes the given pool. A driver may use
// a pool for recycling their own usb requests.
void usb_request_pool_init(usb_request_pool_t* pool, uint64_t node_offset);

// usb_request_pool_add() adds the request to the pool.
zx_status_t usb_request_pool_add(usb_request_pool_t* pool, usb_request_t* req);

// returns a request from the pool that has a buffer of the given length,
// or null if no such request exists.
// The request is not re-initialized in any way and should be set accordingly by the user.
usb_request_t* usb_request_pool_get(usb_request_pool_t* pool, size_t length);

// releases all usb requests stored in the pool.
void usb_request_pool_release(usb_request_pool_t* pool);

// Assumes usb_req_internal_t struct is allocated at parent_req_size offset in a usb request.
// Adds a request to the head of the list using the list_node_t pointer from that struct.
zx_status_t usb_req_list_add_head(list_node_t* list, usb_request_t* req, size_t parent_req_size);
// Assumes usb_req_internal_t is allocated at parent_req_size offset in a usb request. Adds a
// request to the tail of the list using the list_node_t pointer from that internal struct.
zx_status_t usb_req_list_add_tail(list_node_t* list, usb_request_t* req, size_t parent_req_size);
// Assumes usb_req_internal_t is allocated at parent_req_size offset in a usb request. Removes a
// request from the head of the list and returns the usb_request_t.
usb_request_t* usb_req_list_remove_head(list_node_t* list, size_t parent_req_size);

__END_CDECLS

#endif  // SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_USB_REQUEST_H_
