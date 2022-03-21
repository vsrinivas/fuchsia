// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/usb/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/phys-iter.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <ddk/hw/physiter/c/banjo.h>
#include <usb/usb-request.h>

#include "align.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static inline size_t req_buffer_size(usb_request_t* req, size_t offset) {
  size_t remaining = req->size - req->offset - offset;
  // May overflow.
  if (remaining > req->size) {
    remaining = 0;
  }
  return remaining;
}

static inline void* req_buffer_virt(usb_request_t* req) {
  return (void*)(((uintptr_t)req->virt) + req->offset);
}

__EXPORT zx_status_t usb_request_alloc(usb_request_t** out, uint64_t data_size, uint8_t ep_address,
                                       size_t req_size) {
  if (req_size < sizeof(usb_request_t)) {
    return ZX_ERR_INVALID_ARGS;
  }
  usb_request_t* req = calloc(1, req_size);
  if (!req) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status = ZX_OK;
  if (data_size > 0) {
    status = zx_vmo_create(data_size, 0, &req->vmo_handle);
    if (status != ZX_OK) {
      zxlogf(ERROR, "usb_request_alloc: Failed to create vmo: %d", status);
      free(req);
      return status;
    }

    zx_vaddr_t mapped_addr;
    status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0,
                         req->vmo_handle, 0, data_size, &mapped_addr);

    if (status != ZX_OK) {
      zxlogf(ERROR, "usb_request_alloc: Failed to map the vmo: %d", status);
      free(req);
      return status;
    }

    req->virt = mapped_addr;
    req->offset = 0;
    req->size = data_size;
  }
  req->alloc_size = req_size;
  req->header.ep_address = ep_address;
  req->header.length = data_size;
  req->release_frees = true;
  *out = req;
  return ZX_OK;
}

// usb_request_alloc_vmo() creates a new usb request with the given VMO.
__EXPORT zx_status_t usb_request_alloc_vmo(usb_request_t** out, zx_handle_t vmo_handle,
                                           uint64_t vmo_offset, uint64_t length, uint8_t ep_address,
                                           size_t req_size) {
  usb_request_t* req = calloc(1, req_size);
  if (!req) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_handle_t dup_handle;
  zx_status_t status = zx_handle_duplicate(vmo_handle, ZX_RIGHT_SAME_RIGHTS, &dup_handle);
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb_request_alloc_vmo: Failed to duplicate handle: %d", status);
    free(req);
    return status;
  }

  uint64_t size;
  status = zx_vmo_get_size(dup_handle, &size);
  if (status != ZX_OK) {
    zx_handle_close(dup_handle);
    free(req);
    return status;
  }

  zx_vaddr_t mapped_addr;
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, dup_handle, 0,
                       size, &mapped_addr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb_request_alloc_vmo: zx_vmar_map failed %d size: %zu", status, size);
    zx_handle_close(dup_handle);
    free(req);
    return status;
  }

  req->alloc_size = req_size;
  req->vmo_handle = dup_handle;
  req->virt = mapped_addr;
  req->offset = vmo_offset;
  req->size = size;

  req->pmt = ZX_HANDLE_INVALID;

  req->header.ep_address = ep_address;
  req->header.length = length;
  req->release_frees = true;
  *out = req;
  return ZX_OK;
}

// usb_request_init() initializes the statically allocated usb request with the given VMO.
// This will free any resources allocated by the usb request but not the usb request itself.
__EXPORT zx_status_t usb_request_init(usb_request_t* req, zx_handle_t vmo_handle,
                                      uint64_t vmo_offset, uint64_t length, uint8_t ep_address) {
  memset(req, 0, req->alloc_size);

  zx_handle_t dup_handle;
  zx_status_t status = zx_handle_duplicate(vmo_handle, ZX_RIGHT_SAME_RIGHTS, &dup_handle);
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb_request_init: Failed to duplicate handle: %d", status);
    return status;
  }

  uint64_t size;
  status = zx_vmo_get_size(dup_handle, &size);
  if (status != ZX_OK) {
    zx_handle_close(dup_handle);
    return status;
  }

  if (length > size || vmo_offset > size - length) {
    zx_handle_close(dup_handle);
    return ZX_ERR_INVALID_ARGS;
  }

  // TODO(ravoorir): Do not map the entire vmo. Map only what is needed.
  zx_vaddr_t mapped_addr;
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, dup_handle, 0,
                       size, &mapped_addr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb_request_init: zx_vmar_map failed %d size: %zu", status, size);
    zx_handle_close(dup_handle);
    return status;
  }

  req->vmo_handle = dup_handle;
  req->virt = mapped_addr;
  req->offset = vmo_offset;
  req->size = size;

  req->pmt = ZX_HANDLE_INVALID;

  req->header.ep_address = ep_address;
  req->header.length = length;
  req->release_frees = false;
  return ZX_OK;
}

__EXPORT zx_status_t usb_request_set_sg_list(usb_request_t* req, const sg_entry_t* sg_list,
                                             size_t sg_count) {
  if (req->sg_list) {
    free(req->sg_list);
    req->sg_list = NULL;
    req->sg_count = 0;
  }
  size_t total_length = 0;
  // TODO(jocelyndang): disallow overlapping entries?
  for (size_t i = 0; i < sg_count; ++i) {
    const sg_entry_t* entry = &sg_list[i];
    if (entry->length == 0 || (req_buffer_size(req, entry->offset) < entry->length)) {
      return ZX_ERR_INVALID_ARGS;
    }
    total_length += entry->length;
  }
  size_t num_bytes = sg_count * sizeof(sg_entry_t);
  req->sg_list = malloc(num_bytes);
  if (req->sg_list == NULL) {
    zxlogf(ERROR, "usb_request_set_sg_list: out of memory");
    return ZX_ERR_NO_MEMORY;
  }
  memcpy(req->sg_list, sg_list, num_bytes);
  req->sg_count = sg_count;
  req->header.length = total_length;
  return ZX_OK;
}

__EXPORT ssize_t usb_request_copy_from(usb_request_t* req, void* data, size_t length,
                                       size_t offset) {
  length = MIN(req_buffer_size(req, offset), length);
  memcpy(data, req_buffer_virt(req) + offset, length);
  return length;
}

__EXPORT ssize_t usb_request_copy_to(usb_request_t* req, const void* data, size_t length,
                                     size_t offset) {
  length = MIN(req_buffer_size(req, offset), length);
  memcpy(req_buffer_virt(req) + offset, data, length);
  return length;
}

__EXPORT zx_status_t usb_request_mmap(usb_request_t* req, void** data) {
  *data = req_buffer_virt(req);
  // TODO(jocelyndang): modify this once we start passing usb requests across process boundaries.
  return ZX_OK;
}

__EXPORT zx_status_t usb_request_cache_flush(usb_request_t* req, zx_off_t offset, size_t length) {
  if (offset + length < offset || offset + length > req->size) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return zx_cache_flush(req_buffer_virt(req) + offset, length, ZX_CACHE_FLUSH_DATA);
}

__EXPORT zx_status_t usb_request_cache_flush_invalidate(usb_request_t* req, zx_off_t offset,
                                                        size_t length) {
  if (offset + length < offset || offset + length > req->size) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return zx_cache_flush(req_buffer_virt(req) + offset, length,
                        ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
}

zx_status_t usb_request_physmap(usb_request_t* req, zx_handle_t bti_handle) {
  if (req->phys_count > 0) {
    return ZX_OK;
  }
  const size_t kPageSize = zx_system_get_page_size();
  // zx_bti_pin returns whole pages, so take into account unaligned vmo
  // offset and length when calculating the amount of pages returned
  uint64_t page_offset = USB_ROUNDDOWN(req->offset, kPageSize);
  // The buffer size is the vmo size from offset 0.
  uint64_t page_length = req->size - page_offset;
  uint64_t pages = USB_ROUNDUP(page_length, kPageSize) / kPageSize;

  zx_paddr_t* paddrs = malloc(pages * sizeof(zx_paddr_t));
  if (paddrs == NULL) {
    zxlogf(ERROR, "usb_request_physmap: out of memory");
    return ZX_ERR_NO_MEMORY;
  }
  const size_t sub_offset = page_offset & (kPageSize - 1);
  const size_t pin_offset = page_offset - sub_offset;
  const size_t pin_length = USB_ROUNDUP(page_length + sub_offset, kPageSize);

  if (pin_length / kPageSize != pages) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_handle_t pmt;
  uint32_t options = ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE;
  zx_status_t status =
      zx_bti_pin(bti_handle, options, req->vmo_handle, pin_offset, pin_length, paddrs, pages, &pmt);
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb_request_physmap: zx_bti_pin failed:%d", status);
    free(paddrs);
    return status;
  }
  // Account for the initial misalignment if any
  paddrs[0] += sub_offset;
  req->phys_list = paddrs;
  req->phys_count = pages;
  req->pmt = pmt;

  return ZX_OK;
}

__EXPORT void usb_request_release(usb_request_t* req) {
  if (req->vmo_handle != ZX_HANDLE_INVALID) {
    if (req->pmt != ZX_HANDLE_INVALID) {
      zx_status_t status = zx_pmt_unpin(req->pmt);
      ZX_DEBUG_ASSERT(status == ZX_OK);
      req->pmt = ZX_HANDLE_INVALID;
    }

    zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)req->virt, req->size);
    zx_handle_close(req->vmo_handle);
    req->vmo_handle = ZX_HANDLE_INVALID;
  }
  if (req->phys_list && req->pmt != ZX_HANDLE_INVALID) {
    zx_status_t status = zx_pmt_unpin(req->pmt);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    req->pmt = ZX_HANDLE_INVALID;
  }
  free(req->phys_list);
  req->phys_list = NULL;
  req->phys_count = 0;
  free(req->sg_list);
  req->sg_list = NULL;
  req->sg_count = 0;
  if (req->release_frees) {
    free(req);
  }
}

__EXPORT void usb_request_complete(usb_request_t* req, zx_status_t status, zx_off_t actual,
                                   const usb_request_complete_callback_t* complete_cb) {
  usb_request_complete_base(req, status, actual, 0, complete_cb);
}

__EXPORT void usb_request_complete_base(usb_request_t* req, zx_status_t status, zx_off_t actual,
                                        size_t silent_completions_count,
                                        const usb_request_complete_callback_t* complete_cb) {
  req->response.status = status;
  req->response.actual = actual;
  req->response.silent_completions_count = silent_completions_count;

  if (complete_cb) {
    complete_cb->callback(complete_cb->ctx, req);
  }
}

__EXPORT void usb_request_phys_iter_init(phys_iter_t* iter, usb_request_t* req, size_t max_length) {
  phys_iter_buffer_t buf = {.length = req->header.length,
                            .vmo_offset = req->offset,
                            .phys = req->phys_list,
                            .phys_count = req->phys_count,
                            .sg_list = (phys_iter_sg_entry_t*)(req->sg_list),
                            .sg_count = req->sg_count};
  phys_iter_init(iter, &buf, max_length);
}

__EXPORT size_t usb_request_phys_iter_next(phys_iter_t* iter, zx_paddr_t* out_paddr) {
  return phys_iter_next(iter, out_paddr);
}

__EXPORT void usb_request_pool_init(usb_request_pool_t* pool, uint64_t node_offset) {
  mtx_init(&pool->lock, mtx_plain);
  list_initialize(&pool->free_reqs);
  pool->node_offset = node_offset;
}

__EXPORT zx_status_t usb_request_pool_add(usb_request_pool_t* pool, usb_request_t* req) {
  mtx_lock(&pool->lock);
  if (req->alloc_size < (pool->node_offset + sizeof(list_node_t))) {
    mtx_unlock(&pool->lock);
    return ZX_ERR_INVALID_ARGS;
  }
  list_add_tail(&pool->free_reqs, (list_node_t*)((uintptr_t)req + pool->node_offset));
  mtx_unlock(&pool->lock);
  return ZX_OK;
}

__EXPORT usb_request_t* usb_request_pool_get(usb_request_pool_t* pool, size_t length) {
  usb_request_t* req = NULL;
  bool found = false;

  mtx_lock(&pool->lock);
  list_node_t* node;
  list_for_every(&pool->free_reqs, node) {
    req = (usb_request_t*)((uintptr_t)node - pool->node_offset);
    if (req->size == length) {
      found = true;
      break;
    }
  }
  if (found) {
    list_delete(node);
  }
  mtx_unlock(&pool->lock);

  return found ? req : NULL;
}

__EXPORT void usb_request_pool_release(usb_request_pool_t* pool) {
  mtx_lock(&pool->lock);

  usb_request_t* req;
  list_node_t* node;
  while ((node = list_remove_tail(&pool->free_reqs)) != NULL) {
    req = (usb_request_t*)((uintptr_t)node - pool->node_offset);
    usb_request_release(req);
  }

  mtx_unlock(&pool->lock);
}

__EXPORT zx_status_t usb_req_list_add_head(list_node_t* list, usb_request_t* req,
                                           size_t parent_req_size) {
  if (req->alloc_size < parent_req_size + sizeof(list_node_t)) {
    return ZX_ERR_INVALID_ARGS;
  }
  usb_req_internal_t* req_int = USB_REQ_TO_REQ_INTERNAL(req, parent_req_size);
  list_add_head(list, &req_int->node);
  return ZX_OK;
}

__EXPORT zx_status_t usb_req_list_add_tail(list_node_t* list, usb_request_t* req,
                                           size_t parent_req_size) {
  if (req->alloc_size < parent_req_size + sizeof(list_node_t)) {
    return ZX_ERR_INVALID_ARGS;
  }
  usb_req_internal_t* req_int = USB_REQ_TO_REQ_INTERNAL(req, parent_req_size);
  list_add_tail(list, &req_int->node);
  return ZX_OK;
}

__EXPORT usb_request_t* usb_req_list_remove_head(list_node_t* list, size_t parent_req_size) {
  usb_req_internal_t* req_int = list_remove_head_type(list, usb_req_internal_t, node);
  if (req_int) {
    return REQ_INTERNAL_TO_USB_REQ(req_int, parent_req_size);
  }
  return NULL;
}
