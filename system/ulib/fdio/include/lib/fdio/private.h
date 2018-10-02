// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <stdint.h>

__BEGIN_CDECLS;

typedef struct fdio fdio_t;

// Deprecated version of fdio_unsafe_fd_to_io()
fdio_t* __fdio_fd_to_io(int fd);
// Deprecated version of fdio_unsafe_borrow_channel()
zx_handle_t __fdio_borrow_channel(fdio_t* io);
// Deprecated version of fdio_unsafe_release()
void __fdio_release(fdio_t* io);
// Deprecated version of fdio_unsafe_wait_begin()
void __fdio_wait_begin(fdio_t* io, uint32_t events,
                       zx_handle_t* handle_out, zx_signals_t* signals_out);
// Deprecated version of fdio_unsafe_wait_end()
void __fdio_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* events_out);

// WARNING: These APIs are subject to change

// __fdio_cleanpath cleans an input path, placing the output
// in out, which is a buffer of at least "PATH_MAX" bytes.
//
// 'outlen' returns the length of the path placed in out, and 'is_dir'
// is set to true if the returned path must be a directory.
zx_status_t __fdio_cleanpath(const char* in, char* out, size_t* outlen, bool* is_dir);

__END_CDECLS;
