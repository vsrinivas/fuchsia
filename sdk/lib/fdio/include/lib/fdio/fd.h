// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_INCLUDE_LIB_FDIO_FD_H_
#define LIB_FDIO_INCLUDE_LIB_FDIO_FD_H_

#include <zircon/availability.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Create a file descriptor from a `zx_handle_t`.
//
// The `handle` must be to a channel, socket, vmo, or debuglog object. It is always consumed by this
// function. FDs can be created for certain other kernel object types by [fdio_handle_fd()] as long
// as the returned file descriptor will only be used for waiting (rather than reading/writing).
//
// If the `zx_handle_t` is a channel, then the channel must implement the `fuchsia.io.Node`
// protocol.
//
// For more precise control over which file descriptor is allocated, consider using [fdio_create()]
// and [fdio_bind_to_fd()].
//
// Upon success, `fd_out` contains a file descriptor that can be used to access `handle`.
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
zx_status_t fdio_fd_create(zx_handle_t handle, int* fd_out) ZX_AVAILABLE_SINCE(1);

// Clones the current working directory.
//
// Upon success, `out_handle` contains a handle that represents the current working directory.
// Internally, cloning creates a new distinct connection to the directory that can be transferred to
// another process without affecting the current process's directory connection.
//
// # Errors
//
//   * `ZX_ERR_NOT_SUPPORTED`: The cwd cannot be represented as a `zx_handle_t`.
//
//   * `ZX_ERR_BAD_STATE`: The cwd cannot be cloned in its current state.
//
//   * `ZX_ERR_ACCESS_DENIED`: The cwd has insufficient rights to clone the underlying object.
zx_status_t fdio_cwd_clone(zx_handle_t* out_handle) ZX_AVAILABLE_SINCE(1);

// Clones a file descriptor.
//
// Upon success, `out_handle` contains a handle that represents the given file descriptor.
// Internally, cloning creates a new distinct connection to the file or service (via the
// fuchsia.io/Node.Clone FIDL interface) that can be transferred to another process without
// affecting the connection associated with the `fd`. This is more heavyweight than transferring via
// [fdio_fd_transfer()] but can be done in more cases. See also [fdio_fd_transfer_or_clone()].
//
// `fd` is not modified by this function.
//
// # Errors
//
//   * `ZX_ERR_INVALID_ARGS`: `fd` is not a valid file descriptor.
//
//   * `ZX_ERR_NOT_SUPPORTED`: `fd` cannot be represented as a `zx_handle_t`.
//
//   * `ZX_ERR_BAD_STATE`: `fd` cannot be cloned in its current state.
//
//   *` ZX_ERR_ACCESS_DENIED`: `fd` has insufficient rights to clone the underlying object.
zx_status_t fdio_fd_clone(int fd, zx_handle_t* out_handle) ZX_AVAILABLE_SINCE(1);

// Prepares a file descriptor for transfer to another process.
//
// Upon return, the given file descriptor has been removed from the file descriptor table for this
// process.
//
// Upon success, `out_handle` contains a handle that represents the given file descriptor. This
// handle represents the connection to the file or service that was previously represented by the
// `fd`. This behavior is identical to that of fdio_get_service_handle().
//
// A file descriptor may not always be in a state that its underlying handle can be transferred in
// this way. For example, if it has been dup()-ed, there will be more than one descriptor referring
// to the same handle. In these cases, callers may want to use the less-efficient [fdio_fd_clone()]
// to create a new connection to the same file, or [fdio_fd_transfer_or_clone()] to automatically do
// this fallback.
//
// # Errors
//
//   * `ZX_ERR_INVALID_ARGS`: `fd` is not a valid file descriptor.
//
//   * `ZX_ERR_UNAVAILABLE`: `fd` is busy or has been dup'ed and therefore is referenced by multiple
//     entries in the file descriptor table.
//
//   * `ZX_ERR_NOT_SUPPORTED`: `fd` cannot be represented as a `zx_handle_t`.
//
//   * `ZX_ERR_BAD_STATE`: `fd` cannot be transferred to another process in its
//     current state.
//
//   * `ZX_ERR_ACCESS_DENIED`: `fd` has insufficient rights to clone the underlying object.
zx_status_t fdio_fd_transfer(int fd, zx_handle_t* out_handle) ZX_AVAILABLE_SINCE(1);

// Prepares a file descriptor for transfer (as [fdio_fd_transfer()]), or falls-back to cloning (as
// [fdio_fd_clone()] it if the descriptor is not transferrable. FDs that have been dup()ed have
// multiple file descriptors sharing the same kernel handle that prevents doing a simple transfer.
//
// Upon return, the given file descriptor has been removed from the file descriptor table for this
// process.
//
// Upon success, `out_handle` contains a handle that represents the given file descriptor.
//
// # Errors
//
//   * `ZX_ERR_INVALID_ARGS`: `fd` is not a valid file descriptor.
//
//   * `ZX_ERR_UNAVAILABLE`: `fd` is busy.
//
//   * `ZX_ERR_NOT_SUPPORTED`: `fd` cannot be represented as a `zx_handle_t`.
//
//   * `ZX_ERR_BAD_STATE`: `fd` cannot be transferred to another process in its current state.
//
//   * `ZX_ERR_ACCESS_DENIED`: `fd` has insufficient rights to clone the underlying object.
zx_status_t fdio_fd_transfer_or_clone(int fd, zx_handle_t* out_handle) ZX_AVAILABLE_SINCE(1);

__END_CDECLS

#endif  // LIB_FDIO_INCLUDE_LIB_FDIO_FD_H_
