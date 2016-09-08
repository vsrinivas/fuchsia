// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/iotxn.h>
#include <ddk/device.h>
#include <magenta/syscalls.h>
#include <sys/param.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

#define IOTXN_FLAG_CLONE (1 << 0)

typedef struct iotxn_priv iotxn_priv_t;

struct iotxn_priv {
    // data payload. either data buffer or vmo.
    mx_size_t data_size;
    void* data;
    mx_paddr_t data_phys;
    mx_size_t vmo_offset;
    mx_handle_t vmo;

    uint32_t flags;

    // extra data, at the end of this ioxtn_t structure
    mx_size_t extra_size;

    // total size and physical address of buffer containing this structure, minus the
    // size of this structure
    // this field is set on memory allocation and should never be modified afterwards
    mx_size_t buffer_size;
    mx_paddr_t buffer_phys;

    // 64-bytes at this point on 64-bit systems

    iotxn_t txn; // must be at the end for extra data, only valid if not a clone
};

#define get_priv(iotxn) containerof(iotxn, iotxn_priv_t, txn)

static list_node_t free_list = LIST_INITIAL_VALUE(free_list);
static list_node_t clone_list = LIST_INITIAL_VALUE(clone_list); // free list for clones
static mtx_t free_list_mutex = MTX_INIT;
static mtx_t clone_list_mutex = MTX_INIT;

static void iotxn_complete(iotxn_t* txn, mx_status_t status, mx_off_t actual) {
    txn->actual = actual;
    txn->status = status;
    if (txn->complete_cb) {
        txn->complete_cb(txn, txn->cookie);
    }
}

static void iotxn_copyfrom(iotxn_t* txn, void* data, size_t length, size_t offset) {
    iotxn_priv_t* priv = get_priv(txn);
    size_t count = MIN(length, priv->data_size - offset);
    memcpy(data, priv->data + offset, count);
}

static void iotxn_copyto(iotxn_t* txn, const void* data, size_t length, size_t offset) {
    iotxn_priv_t* priv = get_priv(txn);
    size_t count = MIN(length, priv->data_size - offset);
    memcpy(priv->data + offset, data, count);
}

static void iotxn_physmap(iotxn_t* txn, mx_paddr_t* addr) {
    iotxn_priv_t* priv = get_priv(txn);
    *addr = priv->data_phys;
}

static void iotxn_mmap(iotxn_t* txn, void** data) {
    iotxn_priv_t* priv = get_priv(txn);
    *data = priv->data;
}

static mx_status_t iotxn_clone(iotxn_t* txn, iotxn_t** out, size_t extra_size) {
    iotxn_priv_t* priv = get_priv(txn);
    iotxn_t* clone = NULL;
    iotxn_priv_t* cpriv = NULL;
    // look in clone list first for something that fits
    bool found = false;

    mtx_lock(&clone_list_mutex);
    list_for_every_entry (&clone_list, clone, iotxn_t, node) {
        cpriv = get_priv(clone);
        if (cpriv->buffer_size >= extra_size) {
            found = true;
            break;
        }
    }
    // found one that fits, skip allocation
    if (found) {
        list_delete(&clone->node);
        if (cpriv->buffer_size) memset(cpriv + sizeof(iotxn_priv_t), 0, cpriv->buffer_size);
        mtx_unlock(&clone_list_mutex);
        goto out;
    }
    mtx_unlock(&clone_list_mutex);

    // didn't find one that fits, allocate a new one
    size_t sz = sizeof(iotxn_priv_t) + extra_size;
    cpriv = calloc(1, sz); // cloned iotxn's don't have to be in contiguous memory
    if (!cpriv) {
        xprintf("iotxn: out of memory\n");
        return ERR_NO_MEMORY;
    }
    memset(cpriv, 0, sizeof(iotxn_priv_t));

    // copy properties to the new iotxn
    cpriv->buffer_size = extra_size;
out:
    cpriv->flags |= IOTXN_FLAG_CLONE;
    // copy data payload metadata to the clone so the api can just work
    cpriv->data_size = priv->data_size;
    cpriv->data = priv->data;
    cpriv->data_phys = priv->data_phys;
    cpriv->vmo_offset = priv->vmo_offset;
    cpriv->vmo = priv->vmo;
    memcpy(&cpriv->txn, txn, sizeof(iotxn_t));
    cpriv->txn.complete_cb = NULL; // clear the complete cb
    *out = &cpriv->txn;
    return NO_ERROR;
}

static void iotxn_release(iotxn_t* txn) {
    xprintf("iotxn_release: txn=%p\n", txn);
    iotxn_priv_t* priv = get_priv(txn);
    if (priv->flags & IOTXN_FLAG_CLONE) {
        mtx_lock(&clone_list_mutex);
        list_add_tail(&clone_list, &txn->node);
        mtx_unlock(&clone_list_mutex);
    } else {
        mtx_lock(&free_list_mutex);
        list_add_tail(&free_list, &txn->node);
        mtx_unlock(&free_list_mutex);
    }
}

static iotxn_ops_t ops = {
    .complete = iotxn_complete,
    .copyfrom = iotxn_copyfrom,
    .copyto = iotxn_copyto,
    .physmap = iotxn_physmap,
    .mmap = iotxn_mmap,
    .clone = iotxn_clone,
    .release = iotxn_release,
};

mx_status_t iotxn_alloc(iotxn_t** out, uint32_t flags, size_t data_size, size_t extra_size) {
    xprintf("iotxn_alloc: flags=0x%x data_size=0x%zx extra_size=0x%zx\n", flags, data_size, extra_size);
    iotxn_t* txn = NULL;
    iotxn_priv_t* priv = NULL;
    // look in free list first for something that fits
    bool found = false;

    mtx_lock(&free_list_mutex);
    list_for_every_entry (&free_list, txn, iotxn_t, node) {
        priv = get_priv(txn);
        if (priv->buffer_size >= data_size + extra_size) {
            found = true;
            break;
        }
    }
    // found one that fits, skip allocation
    if (found) {
        list_delete(&txn->node);
        memset(&txn, 0, sizeof(iotxn_t) + priv->buffer_size);
        mtx_unlock(&free_list_mutex);
        goto out;
    }
    mtx_unlock(&free_list_mutex);

    // didn't find one that fits, allocate a new one
    size_t sz = sizeof(iotxn_priv_t) + data_size + extra_size;
    mx_paddr_t phys;
    mx_status_t status = mx_alloc_device_memory(get_root_resource(), sz, &phys, (void**)&priv);
    if (status < 0) {
        xprintf("iotxn: out of memory\n");
        return status;
    }
    memset(priv, 0, sz);

    // layout is iotxn_priv_t | extra_size | data
    priv->buffer_size = data_size + extra_size;
    priv->buffer_phys = phys;
out:
    priv->data_size = data_size;
    priv->extra_size = extra_size;
    priv->data = (void*)priv + sizeof(iotxn_priv_t) + extra_size;
    priv->data_phys = priv->buffer_phys + sizeof(iotxn_priv_t) + extra_size;
    priv->txn.ops = &ops;
    *out = &priv->txn;
    xprintf("iotxn_alloc: found=%d txn=%p buffer_size=0x%zx\n", found, &priv->txn, priv->buffer_size);
    return NO_ERROR;
}

void iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    dev->ops->iotxn_queue(dev, txn);
}
