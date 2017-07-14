// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <mxio/io.h>

#include "private.h"

// This defines operations shared by pipe(2) and socketpair(2) primitives.
typedef struct mx_pipe {
    mxio_t io;
    mx_handle_t h;
} mx_pipe_t;

ssize_t mx_pipe_read(mxio_t* io, void* data, size_t len);
ssize_t mx_pipe_write(mxio_t* io, const void* data, size_t len);
mx_status_t mx_pipe_misc(mxio_t* io, uint32_t op, int64_t off, uint32_t maxreply, void* data, size_t len);
mx_status_t mx_pipe_close(mxio_t* io);
mx_status_t mx_pipe_clone(mxio_t* io, mx_handle_t* handles, uint32_t* types);
void mx_pipe_wait_begin(mxio_t* io, uint32_t events, mx_handle_t* handle, mx_signals_t* _signals);
void mx_pipe_wait_end(mxio_t* io, mx_signals_t signals, uint32_t* _events);
mx_status_t mx_pipe_unwrap(mxio_t* io, mx_handle_t* handles, uint32_t* types);
ssize_t mx_pipe_posix_ioctl(mxio_t* io, int req, va_list va);

ssize_t mx_pipe_read_internal(mx_handle_t h, void* data, size_t len, int nonblock);
ssize_t mx_pipe_write_internal(mx_handle_t h, const void* data, size_t len, int nonblock);
