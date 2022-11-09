// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_INCLUDE_LIB_FDIO_FDIO_H_
#define LIB_FDIO_INCLUDE_LIB_FDIO_FDIO_H_

#include <stdint.h>
#include <unistd.h>
#include <zircon/availability.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// An opaque structure that represents the object to which a file descriptor
// refers.
typedef struct fdio fdio_t;

// Creates an `fdio_t` from a `zx_handle_t`.
//
// The `handle` must be to a channel, socket, vmo, or debuglog object. The handle is always consumed
// by this function.
//
// If the `zx_handle_t` is a channel, then the channel must implement the
// `fuchsia.unknown/Queryable` protocol.
//
// # Errors
//
//   * `ZX_ERR_BAD_HANDLE`: The input `handle` is invalid.
//
//   * `ZX_ERR_ACCESS_DENIED`: Then input `handle` does not have the necessary rights.
//
//   * `ZX_ERR_NO_MEMORY`: Memory allocation failed.
//
//   * `ZX_ERR_NOT_SUPPORTED`: The kernel object type is not valid for an FD.
zx_status_t fdio_create(zx_handle_t handle, fdio_t** out_io) ZX_AVAILABLE_SINCE(1);

// Creates an `fdio_t` that does nothing.
fdio_t* fdio_default_create(void) ZX_AVAILABLE_SINCE(7);

// Creates an `fdio_t` that acts as `/dev/null`.
fdio_t* fdio_null_create(void) ZX_AVAILABLE_SINCE(1);

// Access the `zxio_t` field within an `fdio_t`.
typedef struct zxio_tag zxio_t;
zxio_t* fdio_get_zxio(fdio_t* io) ZX_AVAILABLE_SINCE(1);

// Creates a file descriptor that does nothing.
//
// Returns -1 and sets errno if the function is unable to create the file descriptor (e.g., if the
// file descriptor table is full).
int fdio_fd_create_null(void) ZX_AVAILABLE_SINCE(1);

// Install an `fdio_t` in the file descriptor table for this process.
//
// If fd >= 0, request a specific fd, and starting_fd is ignored.
//
// If fd < 0, request the first available fd >= starting_fd.
//
// Upon success, returns the allocated file descriptor. Returns -1 on failure.
//
// Always takes ownership of the given reference to the `fdio_t`. Upon success, that reference is
// owned by the file descriptor table. Upon failure, the reference is released.
int fdio_bind_to_fd(fdio_t* io, int fd, int starting_fd) ZX_AVAILABLE_SINCE(1);

// Removes a file descriptor from the file descriptor table for this process.
//
// Upon success, the `fdio_t` underlying the file descriptor is returned in `io_out`, and the caller
// receives ownership of one reference to `fdio_t`. Specifically, the caller is responsible for
// calling `fdio_unsafe_release` upon success.
//
// # Errors
//
//   * `ZX_ERR_INVALID_ARGS`: `fd` is not a valid file descriptor.
//
//   * `ZX_ERR_UNAVAILABLE`: `fd` is busy or has been dup'ed and therefore is referenced by multiple
//     entries in the file descriptor table.
zx_status_t fdio_unbind_from_fd(int fd, fdio_t** io_out) ZX_AVAILABLE_SINCE(1);

// Returns a handle to the channel backing a file descriptor.
//
// Upon success, a handle to the backing channel is returned in `out`, and the caller receives
// ownership of that handle.
//
// Always removes the file descriptor from the file descriptor table for this process.
//
// # Errors
//
//   * `ZX_ERR_INVALID_ARGS`: `fd` is not a valid file descriptor.
//
//   * `ZX_ERR_NOT_SUPPORTED`: `fd` is not backed by a channel.
//
//   * `ZX_ERR_UNAVAILABLE`: `fd` is busy or has been dup'ed and therefore is referenced by multiple
//     entries in the file descriptor table.
zx_status_t fdio_get_service_handle(int fd, zx_handle_t* out) ZX_AVAILABLE_SINCE(1);

// Storage for a ZXIO object.
//
// See `lib/zxio/ops.h` for more information.
typedef struct zxio_storage zxio_storage_t;

// Creates an `fdio_t` that is backed by a `zxio_t`.
//
// The `zxio_t` is initialized with a null ops table. The `zxio_storage_t` for the `zxio_t` is
// returned via `out_storage`. The client can re-initialize the `zxio_storage_t` to customize the
// behavior of the `zxio_t`.
//
// The returned `zxio_storage_t` is valid for the lifetime of the returned `fdio_t`.
//
// To bind the `fdio_t` to a file descriptor, use `fdio_bind_to_fd`.
//
// Upon success, the caller receives ownership of one reference to `fdio_t`. Specifically, the
// caller is responsible for calling `fdio_unsafe_release()` upon success.
//
// Upon failure, returns NULL.
fdio_t* fdio_zxio_create(zxio_storage_t** out_storage) ZX_AVAILABLE_SINCE(1);

__END_CDECLS

#endif  // LIB_FDIO_INCLUDE_LIB_FDIO_FDIO_H_
