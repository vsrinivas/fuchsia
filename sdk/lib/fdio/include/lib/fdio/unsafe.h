// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_INCLUDE_LIB_FDIO_UNSAFE_H_
#define LIB_FDIO_INCLUDE_LIB_FDIO_UNSAFE_H_

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// WARNING: These interfaces exist to allow integration of fdio file
// descriptors with handle-centric message loops.  If used incorrectly
// they can seriously mess up the state of fdio, fds, etc.

typedef struct fdio fdio_t;

// This looks up a file descriptor, and if it exists,
// upreferences the fdio_t under it and returns that.
// fdio_unsafe_release() must be called later to release
// the reference.
//
// If the fd does not exist, it returns NULL
fdio_t* fdio_unsafe_fd_to_io(int fd);

// Returns the handle corresponding to the underlying fdio,
// if there is one. Returns ZX_HANDLE_INVALID otherwise.
//
// Since this handle is borrowed from the underlying fdio_t, it
// is unsafe to close it or use it after fdio_unsafe_release is called.
zx_handle_t fdio_unsafe_borrow_channel(fdio_t* io);

// Releases a reference on a fdio_t.  Used to "return"
// a fdio_t obtained from fdio_unsafe_fd_to_io() when you're
// done with it.
void fdio_unsafe_release(fdio_t* io);

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
// hold a reference to.  It is not required that fdio_unsafe_wait_end() be
// called after this.
void fdio_unsafe_wait_begin(fdio_t* io, uint32_t events, zx_handle_t* handle_out,
                            zx_signals_t* signals_out);

// This given a set of signals observed on a handle obtained
// from fdio_unsafe_wait_begin() returns a set of posix-style events
// that are indicated.
//
// This function is only safe to call on a fdio_t you
// hold a reference to.
void fdio_unsafe_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* events_out);

__END_CDECLS

#endif  // LIB_FDIO_INCLUDE_LIB_FDIO_UNSAFE_H_
