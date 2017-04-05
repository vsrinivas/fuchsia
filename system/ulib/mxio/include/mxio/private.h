// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <stdint.h>

__BEGIN_CDECLS;

// WARNING: These APIs are subject to change

// __mxio_cleanpath cleans an input path, placing the output
// in out, which is a buffer of at least "PATH_MAX" bytes.
//
// 'outlen' returns the length of the path placed in out, and 'is_dir'
// is set to true if the returned path must be a directory.
mx_status_t __mxio_cleanpath(const char* in, char* out, size_t* outlen, bool* is_dir);

// WARNING: These interfaces exist to allow integration of mxio file
// descriptors with handle-centric message loops.  If used incorrectly
// they can seriously mess up the state of mxio, fds, etc.

typedef struct mxio mxio_t;


// This looks up a file descriptor, and if it exists,
// upreferences the mxio_t under it and returns that.
//
// If the fd does not exist, it returns NULL
mxio_t* __mxio_fd_to_io(int fd);

// Releases a reference on a mxio_t.  Used to "return"
// a mxio_t obtained from __mxio_fd_to_io() when you're
// done with it.
void __mxio_release(mxio_t* io);


// This given a mxio_t, and a bitmask of posix-style events
// (EPOLLIN, EPOLLOUT, EPOLLERR), this returns a handle that
// may be waited upon and a  bitmask of which signals to
// wait on for the desired events.
//
// The handle belongs to the mxio_t, is not duplicated,
// and may be closed() by the mxio library but MUST NOT
// be closed by the caller.
//
// If waiting is not supported by this mxio_t, the returned
// handle is MX_HANDLE_INVALID.
//
// This function is only safe to call on a mxio_t you
// hold a reference to.
void __mxio_wait_begin(mxio_t* io, uint32_t events,
                       mx_handle_t* handle_out, mx_signals_t* signals_out);

// This given a set of signals observed on a handle obtained
// from __mxio_wait_begin() returns a set of posix-style events
// that are indicated.
//
// This function is only safe to call on a mxio_t you
// hold a reference to.
void __mxio_wait_end(mxio_t* io, mx_signals_t signals, uint32_t* events_out);

__END_CDECLS;
