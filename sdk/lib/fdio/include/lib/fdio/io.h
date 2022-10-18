// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_INCLUDE_LIB_FDIO_IO_H_
#define LIB_FDIO_INCLUDE_LIB_FDIO_IO_H_

#include <lib/fdio/limits.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <unistd.h>  // for ssize_t
#include <zircon/availability.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// Flag on handle args in processargs instructing that this fd should be dup'd to 0/1/2 and be used
// for all of stdio.
#define FDIO_FLAG_USE_FOR_STDIO ((uint32_t)0x8000)

// # Wait events
//
// These bit values provide the events to wait for in [fdio_wait_fd()].
#define FDIO_EVT_READABLE POLLIN
#define FDIO_EVT_WRITABLE POLLOUT
#define FDIO_EVT_ERROR POLLERR
#define FDIO_EVT_PEER_CLOSED POLLRDHUP
#define FDIO_EVT_ALL (POLLIN | POLLOUT | POLLERR | POLLRDHUP)

__BEGIN_CDECLS

// Waits until one or more events are pending.
zx_status_t fdio_wait_fd(int fd, uint32_t events, uint32_t* pending, zx_time_t deadline)
    ZX_AVAILABLE_SINCE(1);

// Create a file descriptor that works with wait APIs (epoll, select, etc.) from a handle and
// expected signals (signals_in/signals_out correspond to POLLIN/POLLOUT events respectively).
//
// Unlike a file (see [fdio_fd_create()]), the returned handle can only be used for waiting and
// closing. However, the input handle can be any waitable kernel object, such as an event.
//
// The shared_handle flag indicates if ownership of the handle should be transferred to the FD. When
// true, the caller is expected to manage the lifetime of the handle (it must outlive the usage of
// the FD). When false, the handle will be closed when the returned descriptor is closed.
//
// On success, returns the created file descriptor. On failure, returns -1 and sets errno.
int fdio_handle_fd(zx_handle_t h, zx_signals_t signals_in, zx_signals_t signals_out,
                   bool shared_handle) ZX_AVAILABLE_SINCE(1);

// Creates a pipe. The first argument returns the file descriptor representing the pipe, and the
// second argument returns the handle of the socket used to communicate with the pipe.
//
// Unlike the POSIX pipe() function which returns two file descriptors, this function returns one
// end of the pipe as a file descriptor for use in the current process with fdio, and the other end
// of the pipe as a kernel handle for transferring to another process.
//
// # Errors
//
//   * `ZX_ERR_NO_MEMORY`: Failed due to a lack of memory.
//
//   * `ZX_ERR_NO_RESOURCES`: Failed to bind to the file descriptor.
zx_status_t fdio_pipe_half(int* out_fd, zx_handle_t* out_handle) ZX_AVAILABLE_SINCE(1);

// Get a read-only VMO containing the whole contents of the file. This function creates a clone of
// the underlying VMO using [fdio_get_vmo_clone()] when possible, falling back to eagerly reading
// the contents into a freshly-created VMO. Copying the file data can have significant memory and
// performance implications for large files so this function must be used carefully, but having this
// fallback avoids the many error cases of [fdio_get_vmo_clone()] and [fdio_get_vmo_exact()].
//
// When cloning is successful, the cloned VMO will have at least the semantics of the
// `ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE` flag for the `zx_vmo_create_child()` system call. This
// leaves unspecified whether the returned VMO will reflect subsequent changes in the underlying
// file or not. When the eager-reading fallback happens, the returned VMO will be a non-atomic
// snapshot of the file when it was read. The size of the VMO will not reflect changes to the
// underlying file. As a result, users should not make assumptions about the semantics of the
// returned VMO with respect to file content changes.
//
// See also:
//
//   * [zxio_vmo_get_copy()] which implements the backend of this function.
//   * [fdio_get_vmo_clone()]
//   * [fdio_get_vmo_exact()]
//   * [fdio_get_vmo_exec()]
zx_status_t fdio_get_vmo_copy(int fd, zx_handle_t* out_vmo) ZX_AVAILABLE_SINCE(1);

// Gets VMO containing a read-only child of the underlying VMO representing the file.
//
// Not all file descriptors represent normal files, and no filesystem is required to provide the
// contents of the file as a VMO. Therefore, callers should be prepared for this function to fail.
// Callers that can tolerate the performance and memory implications of an eager copy of the entire
// contents of the file can use [fdio_get_vmo_copy()] which includes automatic fallback.
//
// On success, the returned VMO will have at least the semantics of the
// `ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE` flag for the `zx_vmo_create_child()` system call. This
// leaves unspecified whether the returned VMO will reflect subsequent changes in the underlying
// file or not. The size of the VMO will not reflect changes to the underlying file. These semantics
// match the POSIX `mmap()` `MAP_PRIVATE` flag.
//
// Users requiring a guaranteed snapshot of the file should use `fdio_get_vmo_exact()` and then use
// `zx_vmo_create_child()` with `ZX_VMO_CHILD_SNAPSHOT` to create a snapshot. However, clients
// should be prepared for this to fail in common cases, both because the filesystem is not required
// to supply an exact VMO, and because creating a snapshot has additional restrictions (most
// commonly that the VMO must not be attached to the paging system).
//
// See also:
//
//   * [zxio_vmo_get_clone()] which implements the backend of this function.
//   * [fdio_get_vmo_copy()]
//   * [fdio_get_vmo_exact()]
//   * [fdio_get_vmo_exec()]
zx_status_t fdio_get_vmo_clone(int fd, zx_handle_t* out_vmo) ZX_AVAILABLE_SINCE(1);

// Get a read-only handle to the exact VMO used by the file system server to represent the file.
// This VMO will track size and content changes to the file.
//
// Not all file descriptors represent normal files, and no filesystem is required to provide the
// contents of the file as a VMO. Therefore, callers should be prepared for this function to fail.
// Callers that can tolerate the performance and memory implications of an eager copy of the entire
// contents of the file can use `fdio_get_vmo_copy()` which includes automatic fallback.
//
// See also:
//
//   * [zxio_vmo_get_clone()] which implements the backend of this function.
//   * [fdio_get_vmo_clone()]
//   * [fdio_get_vmo_copy()]
//   * [fdio_get_vmo_exec()]
zx_status_t fdio_get_vmo_exact(int fd, zx_handle_t* out_vmo) ZX_AVAILABLE_SINCE(1);

// Get a VMO containing a read-only executable child of the underlying VMO. This function will fail
// rather than copying the contents if it cannot be cloned.
//
// This function is identical to `fdio_get_vmo_clone()` except it adds executable rights. See that
// function for more information.
//
// See also:
//
//   * [zxio_vmo_get_exec()] which implements the backend of this function.
//   * [fdio_get_vmo_clone()]
//   * [fdio_get_vmo_copy()]
//   * [fdio_get_vmo_exact()]
zx_status_t fdio_get_vmo_exec(int fd, zx_handle_t* out_vmo) ZX_AVAILABLE_SINCE(1);

__END_CDECLS

#endif  // LIB_FDIO_INCLUDE_LIB_FDIO_IO_H_
