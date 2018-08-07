// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/usb.h>
#include <ddk/usb-request/usb-request.h>
#include <ddk/debug.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// Frees any resources allocated by the usb request, but not the usb request itself.
static void usb_request_release_static(usb_request_t* req) {
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
}

// Frees any resources allocated by the usb request, as well as the usb request itself.
static void usb_request_release_free(usb_request_t* req) {
    usb_request_release_static(req);
    free(req);
}

zx_status_t usb_request_alloc(usb_request_t** out, zx_handle_t bti_handle, uint64_t data_size,
                              uint8_t ep_address) {
    usb_request_t* req = calloc(1, sizeof(usb_request_t));
    if (!req) {
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t status = ZX_OK;
    if (data_size > 0) {
        status = zx_vmo_create(data_size, 0, &req->vmo_handle);
        if (status != ZX_OK) {
            zxlogf(ERROR, "usb_request_alloc: Failed to create vmo: %d\n", status);
            free(req);
            return status;
        }

        zx_vaddr_t mapped_addr;
        status = zx_vmar_map(zx_vmar_root_self(), 0, req->vmo_handle, 0, data_size,
                             ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                             &mapped_addr);

        if (status != ZX_OK) {
            zxlogf(ERROR, "usb_request_alloc: Failed to map the vmo: %d\n", status);
            free(req);
            return status;
        }

        req->virt = (void *)mapped_addr;
        req->offset = 0;
        req->size = data_size;
        req->bti_handle = bti_handle;
    }
    req->header.ep_address = ep_address;
    req->header.length = data_size;
    req->release_cb = usb_request_release_free;
    *out = req;
    return ZX_OK;
}

// usb_request_alloc_vmo() creates a new usb request with the given VMO.
zx_status_t usb_request_alloc_vmo(usb_request_t** out, zx_handle_t bti_handle,
                                  zx_handle_t vmo_handle, uint64_t vmo_offset, uint64_t length,
                                  uint8_t ep_address) {
    usb_request_t* req = calloc(1, sizeof(usb_request_t));
    if (!req) {
        return ZX_ERR_NO_MEMORY;
    }
    zx_handle_t dup_handle;
    zx_status_t status = zx_handle_duplicate(vmo_handle, ZX_RIGHT_SAME_RIGHTS, &dup_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_request_alloc_vmo: Failed to duplicate handle: %d\n", status);
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
    status = zx_vmar_map(zx_vmar_root_self(), 0, dup_handle, 0, size,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &mapped_addr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_request_alloc_vmo: zx_vmar_map failed %d size: %zu\n", status, size);
        zx_handle_close(dup_handle);
        free(req);
        return status;
    }

    req->vmo_handle = dup_handle;
    req->virt = (void *)mapped_addr;
    req->offset = vmo_offset;
    req->size = size;
    req->bti_handle = bti_handle;

    req->pmt = ZX_HANDLE_INVALID;

    req->header.ep_address = ep_address;
    req->header.length = length;
    req->release_cb = usb_request_release_free;
    *out = req;
    return ZX_OK;
}

// usb_request_init() initializes the statically allocated usb request with the given VMO.
// This will free any resources allocated by the usb request but not the usb request itself.
zx_status_t usb_request_init(usb_request_t* req, zx_handle_t bti_handle, zx_handle_t vmo_handle,
                             uint64_t vmo_offset, uint64_t length, uint8_t ep_address) {
    memset(req, 0, sizeof(*req));

    zx_handle_t dup_handle;
    zx_status_t status = zx_handle_duplicate(vmo_handle, ZX_RIGHT_SAME_RIGHTS, &dup_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_request_init: Failed to duplicate handle: %d\n", status);
        return status;
    }

    uint64_t size;
    status = zx_vmo_get_size(dup_handle, &size);
    if (status != ZX_OK) {
        zx_handle_close(dup_handle);
        return status;
    }

    //TODO(ravoorir): Do not map the entire vmo. Map only what is needed.
    zx_vaddr_t mapped_addr;
    status = zx_vmar_map(zx_vmar_root_self(), 0, dup_handle, 0, size,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &mapped_addr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_request_init: zx_vmar_map failed %d size: %zu\n", status, size);
        zx_handle_close(dup_handle);
        return status;
    }

    req->vmo_handle = dup_handle;
    req->virt = (void *)mapped_addr;
    req->offset = vmo_offset;
    req->size = size;
    req->bti_handle = bti_handle;

    req->pmt = ZX_HANDLE_INVALID;

    req->header.ep_address = ep_address;
    req->header.length = length;
    req->release_cb = usb_request_release_static;
    return ZX_OK;
}

ssize_t usb_request_copyfrom(usb_request_t* req, void* data, size_t length, size_t offset) {
    length = MIN(req_buffer_size(req, offset), length);
    memcpy(data, req_buffer_virt(req) + offset, length);
    return length;
}

ssize_t usb_request_copyto(usb_request_t* req, const void* data, size_t length, size_t offset) {
    length = MIN(req_buffer_size(req, offset), length);
    memcpy(req_buffer_virt(req) + offset, data, length);
    return length;
}

zx_status_t usb_request_mmap(usb_request_t* req, void** data) {
    *data = req_buffer_virt(req);
    // TODO(jocelyndang): modify this once we start passing usb requests across process boundaries.
    return ZX_OK;
}

zx_status_t usb_request_cacheop(usb_request_t* req, uint32_t op, size_t offset, size_t length) {
    if (length > 0) {
        return zx_vmo_op_range(req->vmo_handle, op, req->offset + offset, length, NULL, 0);
    } else {
        return ZX_OK;
    }
}

zx_status_t usb_request_cache_flush(usb_request_t* req, zx_off_t offset, size_t length) {
    if (offset + length < offset || offset + length > req->size) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return zx_cache_flush(req_buffer_virt(req) + offset, length, ZX_CACHE_FLUSH_DATA);
}

zx_status_t usb_request_cache_flush_invalidate(usb_request_t* req, zx_off_t offset, size_t length) {
    if (offset + length < offset || offset + length > req->size) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return zx_cache_flush(req_buffer_virt(req) + offset, length,
                          ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
}

zx_status_t usb_request_physmap(usb_request_t* req) {
    if (req->phys_count > 0) {
        return ZX_OK;
    }
    // zx_bti_pin returns whole pages, so take into account unaligned vmo
    // offset and length when calculating the amount of pages returned
    uint64_t page_offset = ROUNDDOWN(req->offset, PAGE_SIZE);
    // The buffer size is the vmo size from offset 0.
    uint64_t page_length = req->size - page_offset;
    uint64_t pages = ROUNDUP(page_length, PAGE_SIZE) / PAGE_SIZE;

    zx_paddr_t* paddrs = malloc(pages * sizeof(zx_paddr_t));
    if (paddrs == NULL) {
        zxlogf(ERROR, "usb_request_physmap: out of memory\n");
        return ZX_ERR_NO_MEMORY;
    }
    const size_t sub_offset = page_offset & (PAGE_SIZE - 1);
    const size_t pin_offset = page_offset - sub_offset;
    const size_t pin_length = ROUNDUP(page_length + sub_offset, PAGE_SIZE);

    if (pin_length / PAGE_SIZE != pages) {
        return ZX_ERR_INVALID_ARGS;
    }
    zx_handle_t pmt;
    uint32_t options = ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE;
    zx_status_t status = zx_bti_pin(req->bti_handle, options, req->vmo_handle,
                                    pin_offset, pin_length, paddrs, pages, &pmt);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_request_physmap: zx_bti_pin failed:%d\n", status);
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

void usb_request_release(usb_request_t* req) {
    if (req->release_cb) {
        req->release_cb(req);
    }
}

void usb_request_complete(usb_request_t* req, zx_status_t status, zx_off_t actual) {
    req->response.status = status;
    req->response.actual = actual;

    if (req->cb_on_error_only && req->response.status == ZX_OK) {
        return;
    }
    if (req->complete_cb) {
        req->complete_cb(req, req->cookie);
    }
}

void usb_request_phys_iter_init(phys_iter_t* iter, usb_request_t* req, size_t max_length) {
    phys_iter_buffer_t buf = {
        .length = req->header.length,
        .vmo_offset = req->offset,
        .phys = req->phys_list,
        .phys_count = req->phys_count,
    };
    phys_iter_init(iter, &buf, max_length);
}

size_t usb_request_phys_iter_next(phys_iter_t* iter, zx_paddr_t* out_paddr) {
    return phys_iter_next(iter, out_paddr);
}

void usb_request_pool_init(usb_request_pool_t* pool) {
    mtx_init(&pool->lock, mtx_plain);
    list_initialize(&pool->free_reqs);
}

void usb_request_pool_add(usb_request_pool_t* pool, usb_request_t* req) {
    mtx_lock(&pool->lock);
    list_add_tail(&pool->free_reqs, &req->node);
    mtx_unlock(&pool->lock);
}

usb_request_t* usb_request_pool_get(usb_request_pool_t* pool, size_t length) {
    usb_request_t* req = NULL;
    bool found = false;

    mtx_lock(&pool->lock);
    list_for_every_entry (&pool->free_reqs, req, usb_request_t, node) {
        if (req->size == length) {
            found = true;
            break;
        }
    }
    if (found) {
        list_delete(&req->node);
    }
    mtx_unlock(&pool->lock);

    return found ? req : NULL;
}

void usb_request_pool_release(usb_request_pool_t* pool) {
    mtx_lock(&pool->lock);

    usb_request_t* req;
    while ((req = list_remove_tail_type(&pool->free_reqs, usb_request_t, node)) != NULL) {
        usb_request_release(req);
    }

    mtx_unlock(&pool->lock);
}
