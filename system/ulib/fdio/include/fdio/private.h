// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <stdint.h>

__BEGIN_CDECLS;

// WARNING: These APIs are subject to change

// __fdio_cleanpath cleans an input path, placing the output
// in out, which is a buffer of at least "PATH_MAX" bytes.
//
// 'outlen' returns the length of the path placed in out, and 'is_dir'
// is set to true if the returned path must be a directory.
zx_status_t __fdio_cleanpath(const char* in, char* out, size_t* outlen, bool* is_dir);

// WARNING: These interfaces exist to allow integration of fdio file
// descriptors with handle-centric message loops.  If used incorrectly
// they can seriously mess up the state of fdio, fds, etc.

typedef struct fdio fdio_t;


// This looks up a file descriptor, and if it exists,
// upreferences the fdio_t under it and returns that.
//
// If the fd does not exist, it returns NULL
fdio_t* __fdio_fd_to_io(int fd);

// Releases a reference on a fdio_t.  Used to "return"
// a fdio_t obtained from __fdio_fd_to_io() when you're
// done with it.
void __fdio_release(fdio_t* io);


// This given a fdio_t, and a bitmask of posix-style events
// (EPOLLIN, EPOLLOUT, EPOLLERR), this returns a handle that
// may be waited upon and a  bitmask of which signals to
// wait on for the desired events.
//
// The handle belongs to the fdio_t, is not duplicated,
// and may be closed() by the fdio library but MUST NOT
// be closed by the caller.
//
// If waiting is not supported by this fdio_t, the returned
// handle is ZX_HANDLE_INVALID.
//
// This function is only safe to call on a fdio_t you
// hold a reference to.
void __fdio_wait_begin(fdio_t* io, uint32_t events,
                       zx_handle_t* handle_out, zx_signals_t* signals_out);

// This given a set of signals observed on a handle obtained
// from __fdio_wait_begin() returns a set of posix-style events
// that are indicated.
//
// This function is only safe to call on a fdio_t you
// hold a reference to.
void __fdio_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* events_out);

__END_CDECLS;
