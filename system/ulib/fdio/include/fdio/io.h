// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <unistd.h> // for ssize_t

#include <zircon/types.h>
#include <zircon/compiler.h>

#include <fdio/limits.h>

// flag on handle args in processargs
// instructing that this fd should be dup'd to 0/1/2
// and be used for all of stdio
#define FDIO_FLAG_USE_FOR_STDIO 0x8000

#define FDIO_NONBLOCKING 1

// TODO(smklein): Assert that these align with
// the ObjectInfo tags
#define FDIO_PROTOCOL_SERVICE 0
#define FDIO_PROTOCOL_REMOTE FDIO_PROTOCOL_SERVICE // Deprecated
#define FDIO_PROTOCOL_FILE 1
#define FDIO_PROTOCOL_DIRECTORY 2
#define FDIO_PROTOCOL_PIPE 3
#define FDIO_PROTOCOL_VMOFILE 4
#define FDIO_PROTOCOL_DEVICE 5
#define FDIO_PROTOCOL_SOCKET 6
#define FDIO_PROTOCOL_SOCKET_CONNECTED 7

// events for fdio_wait_fd()
#define FDIO_EVT_READABLE POLLIN
#define FDIO_EVT_WRITABLE POLLOUT
#define FDIO_EVT_ERROR POLLERR
#define FDIO_EVT_PEER_CLOSED POLLRDHUP
#define FDIO_EVT_ALL (POLLIN | POLLOUT | POLLERR | POLLRDHUP)

__BEGIN_CDECLS

// wait until one or more events are pending
zx_status_t fdio_wait_fd(int fd, uint32_t events, uint32_t* pending, zx_time_t deadline);

// create a fd that works with wait APIs (epoll, select, etc.) from a handle
// and expected signals (signals_in/signals_out correspond to POLLIN/POLLOUT
// events respectively). the handle will be closed when the fd is closed, unless
// shared_handle is true.
int fdio_handle_fd(zx_handle_t h, zx_signals_t signals_in, zx_signals_t signals_out, bool shared_handle);

// invoke a raw fdio ioctl
ssize_t fdio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len);

// create a pipe, installing one half in a fd, returning the other
// for transport to another process
zx_status_t fdio_pipe_half(zx_handle_t* handle, uint32_t* type);

// Get a read-only VMO containing the whole contents of the file.
// This function creates a clone of the underlying VMO when possible, falling
// back to eagerly reading the contents into a freshly-created VMO.
//
// DEPRECATED: Prefer the new name "fdio_get_vmo_copy" or
// "fdio_get_vmo_clone", explicit about clone/copy behavior.
zx_status_t fdio_get_vmo(int fd, zx_handle_t* out_vmo) __DEPRECATE;

// Get a read-only VMO containing the whole contents of the file.
// This function creates a clone of the underlying VMO when possible, falling
// back to eagerly reading the contents into a freshly-created VMO.
zx_status_t fdio_get_vmo_copy(int fd, zx_handle_t* out_vmo);

// Gets a read-only VMO containing a clone of the underlying VMO.
// This function will fail rather than copying the contents if it cannot clone.
zx_status_t fdio_get_vmo_clone(int fd, zx_handle_t* out_vmo);

// Get a read-only handle to the exact VMO used by the file system server to
// represent the file. This function fails if the server does not have an exact
// VMO representation of the file (e.g., if fdio_get_vmo would need to copy
// or clone data into a new VMO).
zx_status_t fdio_get_vmo_exact(int fd, zx_handle_t* out_vmo);
// DEPRECATED: Use "fdio_get_vmo_exact".
zx_status_t fdio_get_exact_vmo(int fd, zx_handle_t* out_vmo) __DEPRECATE;

// create a fd that is backed by the given range of the vmo.
// This function takes ownership of the vmo and will close the vmo when the fd
// is closed.
int fdio_vmo_fd(zx_handle_t vmo, uint64_t offset, uint64_t length);

__END_CDECLS
