// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <fdio/io.h>

#include "private.h"

// This defines operations shared by pipe(2) and socketpair(2) primitives.
typedef struct zx_pipe {
    fdio_t io;
    zx_handle_t h;
} zx_pipe_t;

ssize_t zx_pipe_read(fdio_t* io, void* data, size_t len);
ssize_t zx_pipe_write(fdio_t* io, const void* data, size_t len);
zx_status_t zx_pipe_misc(fdio_t* io, uint32_t op, int64_t off, uint32_t maxreply, void* data, size_t len);
zx_status_t zx_pipe_close(fdio_t* io);
zx_status_t zx_pipe_clone(fdio_t* io, zx_handle_t* handles, uint32_t* types);
void zx_pipe_wait_begin(fdio_t* io, uint32_t events, zx_handle_t* handle, zx_signals_t* _signals);
void zx_pipe_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* _events);
zx_status_t zx_pipe_unwrap(fdio_t* io, zx_handle_t* handles, uint32_t* types);
ssize_t zx_pipe_posix_ioctl(fdio_t* io, int req, va_list va);

ssize_t zx_pipe_read_internal(zx_handle_t h, void* data, size_t len, int nonblock);
ssize_t zx_pipe_write_internal(zx_handle_t h, const void* data, size_t len, int nonblock);
