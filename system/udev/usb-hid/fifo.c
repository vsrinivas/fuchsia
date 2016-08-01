// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "fifo.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define min(a,b) ((a) < (b) ? (a) : (b))

mx_status_t usb_hid_fifo_create(usb_hid_fifo_t** fifo) {
    *fifo = malloc(sizeof(usb_hid_fifo_t));
    if (*fifo == NULL)
        return ERR_NO_MEMORY;
    usb_hid_fifo_init(*fifo);
    return NO_ERROR;
}

void usb_hid_fifo_init(usb_hid_fifo_t* fifo) {
    memset(fifo->buf, 0, HID_FIFO_SIZE);
    fifo->head = fifo->tail = 0;
    fifo->empty = true;
}

size_t usb_hid_fifo_size(usb_hid_fifo_t* fifo) {
    if (fifo->empty) return 0;
    if (fifo->head > fifo->tail)
        return fifo->head - fifo->tail;
    return HID_FIFO_SIZE - fifo->tail + fifo->head;
}

ssize_t usb_hid_fifo_peek(usb_hid_fifo_t* fifo, uint8_t* out) {
    if (fifo->empty)
        return 0;
    *out = fifo->buf[fifo->tail];
    return 1;
}

ssize_t usb_hid_fifo_read(usb_hid_fifo_t* fifo, uint8_t* buf, size_t len) {
    if (!buf) return ERR_INVALID_ARGS;
    if (fifo->empty) return 0;
    if (!len) return 0;

    len = min(usb_hid_fifo_size(fifo), len);
    for (size_t c = len; c > 0; c--, fifo->tail = (fifo->tail + 1) & HID_FIFO_MASK) {
        *buf++ = fifo->buf[fifo->tail];
    }
    if (fifo->tail == fifo->head) fifo->empty = true;
    return len;
}

ssize_t usb_hid_fifo_write(usb_hid_fifo_t* fifo, uint8_t* buf, size_t len) {
    if (!fifo->empty && fifo->tail == fifo->head) return ERR_NOT_ENOUGH_BUFFER;
    if (len > HID_FIFO_SIZE - usb_hid_fifo_size(fifo)) return ERR_NOT_ENOUGH_BUFFER;

    for (size_t c = len; c > 0; c--, fifo->head = (fifo->head + 1) & HID_FIFO_MASK) {
        fifo->buf[fifo->head] = *buf++;
    }
    fifo->empty = false;
    return len;
}

void usb_hid_fifo_dump(usb_hid_fifo_t* fifo) {
    printf("usb_hid_fifo_dump %p\n", fifo);
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
