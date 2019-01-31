// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

// fifo must be a power of 2 for the math to work
#define FIFOSIZE 32768
#define FIFOMASK (FIFOSIZE - 1)

typedef struct {
    zx_device_t* zxdev;
    mtx_t lock;
    uint32_t head;
    uint32_t tail;
    char data[FIFOSIZE];
} fifodev_t;

static size_t fifo_readable(fifodev_t* fifo) {
    return (fifo->head - fifo->tail) & FIFOMASK;
}

static size_t fifo_writable(fifodev_t* fifo) {
    return FIFOMASK - ((fifo->head - fifo->tail) & FIFOMASK);
}

static size_t fifo_put(fifodev_t* fifo, const void* buf, size_t len) {
    size_t count = fifo_writable(fifo);
    uint32_t pos = fifo->head & FIFOMASK;
    size_t space = FIFOSIZE - pos;
    if (count > space) { // don't wrap around (single copy)
        count = space;
    }
    if (count > len) { // limit to requested count
        count = len;
    }
    memcpy(fifo->data + pos, buf, count);
    fifo->head += count;
    return count;
}

static size_t fifo_get(fifodev_t* fifo, void* buf, size_t len) {
    size_t count = fifo_readable(fifo);
    uint32_t pos = fifo->tail & FIFOMASK;
    size_t space = FIFOSIZE - pos;
    if (count > space) { // don't wrap around (single copy)
        count = space;
    }
    if (count > len) { // limit to requested count
        count = len;
    }
    memcpy(buf, fifo->data + pos, count);
    fifo->tail += count;
    return count;
}


static zx_status_t fifo_read(void* ctx, void* buf, size_t len,
                             zx_off_t off, size_t* actual) {
    fifodev_t* fifo = ctx;

    mtx_lock(&fifo->lock);
    size_t n = 0;
    size_t count;
    while ((count = fifo_get(fifo, buf, len)) > 0) {
        len -= count;
        buf += count;
        n += count;
    }
    if (n == 0) {
        device_state_clr(fifo->zxdev, DEV_STATE_READABLE);
    } else {
        device_state_set(fifo->zxdev, DEV_STATE_WRITABLE);
    }
    mtx_unlock(&fifo->lock);
    *actual = n;

    return (n == 0) ? ZX_ERR_SHOULD_WAIT : ZX_OK;
}

static zx_status_t fifo_write(void* ctx, const void* buf, size_t len,
                              zx_off_t off, size_t* actual) {

    fifodev_t* fifo = ctx;

    mtx_lock(&fifo->lock);
    size_t n = 0;
    size_t count;
    while ((count = fifo_put(fifo, buf, len)) > 0) {
        len -= count;
        buf += count;
        n += count;
    }
    if (n == 0) {
        device_state_clr(fifo->zxdev, DEV_STATE_WRITABLE);
    } else {
        device_state_set(fifo->zxdev, DEV_STATE_READABLE);
    }
    mtx_unlock(&fifo->lock);
    *actual = n;

    return (n == 0) ? ZX_ERR_SHOULD_WAIT : ZX_OK;
}

static void fifo_release(void* ctx) {
    fifodev_t* fifo = ctx;
    free(fifo);
}

static zx_protocol_device_t fifo_ops = {
    .version = DEVICE_OPS_VERSION,
    .read = fifo_read,
    .write = fifo_write,
    .release = fifo_release,
};

static zx_status_t fifo_bind(void* ctx, zx_device_t* parent) {
    fifodev_t* fifo = calloc(1, sizeof(fifodev_t));
    if (fifo == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    mtx_init(&fifo->lock, mtx_plain);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "demo-fifo",
        .ctx = fifo,
        .ops = &fifo_ops,
    };
    zx_status_t status = device_add(parent, &args, &fifo->zxdev);
    if (status != ZX_OK) {
        free(fifo);
        return status;
    }

    // initially we're empty, so writable but not readable
    device_state_set(fifo->zxdev, DEV_STATE_WRITABLE);

    return ZX_OK;
}

static zx_driver_ops_t fifo_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = fifo_bind,
};

ZIRCON_DRIVER_BEGIN(demo_fifo, fifo_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(demo_fifo)
