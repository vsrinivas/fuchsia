// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <magenta/compiler.h>

__BEGIN_CDECLS;

#define PTY_FIFO_SIZE (4096)

typedef struct pty_fifo {
    uint32_t head;
    uint32_t tail;
    uint8_t data[PTY_FIFO_SIZE];
} pty_fifo_t;

size_t pty_fifo_read(pty_fifo_t* fifo, void* data, size_t len);
size_t pty_fifo_write(pty_fifo_t* fifo, const void* data, size_t len, bool atomic);

static inline bool pty_fifo_is_empty(pty_fifo_t* fifo) {
    return fifo->head == fifo->tail;
}

static inline bool pty_fifo_is_full(pty_fifo_t* fifo) {
    return (fifo->head - fifo->tail) == PTY_FIFO_SIZE;
}

__END_CDECLS;
