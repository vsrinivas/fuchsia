// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <string.h>

#include "pty-fifo.h"

static_assert((PTY_FIFO_SIZE & (PTY_FIFO_SIZE - 1)) == 0, "fifo size not power of two");

#define PTY_FIFO_MASK (PTY_FIFO_SIZE - 1)

size_t pty_fifo_write(pty_fifo_t* fifo, const void* data, size_t len, bool atomic) {
    size_t avail = PTY_FIFO_SIZE - (fifo->head - fifo->tail);
    if (avail < len) {
        if (atomic) {
            return 0;
        }
        len = avail;
    }

    size_t offset = fifo->head & PTY_FIFO_MASK;

    avail = PTY_FIFO_SIZE - offset;
    if (len <= avail) {
        memcpy(fifo->data + offset, data, len);
    } else {
        memcpy(fifo->data + offset, data, avail);
        memcpy(fifo->data, data + avail, len - avail);
    }

    fifo->head += len;
    return len;
}

size_t pty_fifo_read(pty_fifo_t* fifo, void* data, size_t len) {
    size_t avail = fifo->head - fifo->tail;
    if (avail < len) {
        len = avail;
    }

    size_t offset = fifo->tail & PTY_FIFO_MASK;

    avail = PTY_FIFO_SIZE - offset;
    if (len <= avail) {
        memcpy(data, fifo->data + offset, len);
    } else {
        memcpy(data, fifo->data + offset, avail);
        memcpy(data + avail, fifo->data, len - avail);
    }

    fifo->tail += len;
    return len;
}

