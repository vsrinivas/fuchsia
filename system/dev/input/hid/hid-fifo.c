// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hid-fifo.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define min(a,b) ((a) < (b) ? (a) : (b))

mx_status_t mx_hid_fifo_create(mx_hid_fifo_t** fifo) {
    *fifo = malloc(sizeof(mx_hid_fifo_t));
    if (*fifo == NULL)
        return MX_ERR_NO_MEMORY;
    mx_hid_fifo_init(*fifo);
    return MX_OK;
}

void mx_hid_fifo_init(mx_hid_fifo_t* fifo) {
    memset(fifo->buf, 0, HID_FIFO_SIZE);
    fifo->head = fifo->tail = 0;
    fifo->empty = true;
    mtx_init(&fifo->lock, mtx_plain);
}

size_t mx_hid_fifo_size(mx_hid_fifo_t* fifo) {
    if (fifo->empty) return 0;
    if (fifo->head > fifo->tail)
        return fifo->head - fifo->tail;
    return HID_FIFO_SIZE - fifo->tail + fifo->head;
}

ssize_t mx_hid_fifo_peek(mx_hid_fifo_t* fifo, void* out) {
    if (fifo->empty)
        return 0;
    *(uint8_t*)out = fifo->buf[fifo->tail];
    return 1;
}

ssize_t mx_hid_fifo_read(mx_hid_fifo_t* fifo, void* buf, size_t len) {
    if (!buf) return MX_ERR_INVALID_ARGS;
    if (fifo->empty) return 0;
    if (!len) return 0;

    len = min(mx_hid_fifo_size(fifo), len);
    for (size_t c = len; c > 0; c--, fifo->tail = (fifo->tail + 1) & HID_FIFO_MASK) {
        *(uint8_t*)buf++ = fifo->buf[fifo->tail];
    }
    if (fifo->tail == fifo->head) fifo->empty = true;
    return len;
}

ssize_t mx_hid_fifo_write(mx_hid_fifo_t* fifo, const void* buf, size_t len) {
    if (!fifo->empty && fifo->tail == fifo->head) return MX_ERR_BUFFER_TOO_SMALL;
    if (len > HID_FIFO_SIZE - mx_hid_fifo_size(fifo)) return MX_ERR_BUFFER_TOO_SMALL;

    for (size_t c = len; c > 0; c--, fifo->head = (fifo->head + 1) & HID_FIFO_MASK) {
        fifo->buf[fifo->head] = *(uint8_t*)buf++;
    }
    fifo->empty = false;
    return len;
}

void mx_hid_fifo_dump(mx_hid_fifo_t* fifo) {
    printf("mx_hid_fifo_dump %p\n", fifo);
    printf("head: %u  tail: %u  empty: %s\n", fifo->head, fifo->tail, fifo->empty ? "Y" : "N");
    if (fifo->empty) {
        return;
    }
    uint32_t c = fifo->tail;
    int i = 0;
    do {
        printf("%02x ", fifo->buf[c]);
        if (i++ % 8 == 7) printf("\n");
        c = (c + 1) & HID_FIFO_MASK;
    } while (c != fifo->head);
    printf("\n");
}
