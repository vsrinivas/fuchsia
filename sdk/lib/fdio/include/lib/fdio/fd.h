// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_INCLUDE_LIB_FDIO_FD_H_
#define LIB_FDIO_INCLUDE_LIB_FDIO_FD_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Create a file descriptor from a |zx_handle_t|.
//
// The |handle| must be to a channel, socket, vmo, or debuglog object.
//
// If the |zx_handle_t| is a channel, then the channel must implement the
// |fuchsia.io.Node| protocol.
//
// For more precise control over which file descriptor is allocated, consider
// using |fdio_create| and |fdio_bind_to_fd|.
//
// Always consumes |handle|.
//
// Upon success, |fd_out| contains a file descriptor that can be used to access
// |handle|.
//
// # Errors
//
// TODO: Catalog errors.
zx_status_t fdio_fd_create(zx_handle_t handle, int* fd_out);

// Clones the current working directory.
//
// Upon success, |out_handle| contains a handle that represents the current
// working directory. This handle is suitable for transferring to another
// process.
//
// # Errors
//
// ZX_ERR_NOT_SUPPORTED: The cwd cannot be represented as a |zx_handle_t|.
//
// ZX_ERR_BAD_STATE: The cwd cannot be cloned in its current state.
//
// ZX_ERR_ACCESS_DENIED: The cwd has insufficient rights to clone the underlying
// object.
zx_status_t fdio_cwd_clone(zx_handle_t* out_handle);

// Clones a file descriptor.
//
// Upon success, |out_handle| contains a handle that represents the given file
// descriptor. This handle is suitable for transferring to another process.
//
// |fd| is not modified by this function.
//
// # Errors
//
// ZX_ERR_INVALID_ARGS: |fd| is not a valid file descriptor.
//
// ZX_ERR_NOT_SUPPORTED: |fd| cannot be represented as a |zx_handle_t|.
//
// ZX_ERR_BAD_STATE: |fd| cannot be cloned in its current state.
//
// ZX_ERR_ACCESS_DENIED: |fd| has insufficient rights to clone the underlying
// object.
zx_status_t fdio_fd_clone(int fd, zx_handle_t* out_handle);

// Prepares a file descriptor for transfer to another process.
//
// Upon return, the given file descriptor has been removed from the file
// descriptor table for this process.
//
// Upon success, |out_handle| contains a handle that represents the given file
// descriptor.
//
// # Errors
//
// ZX_ERR_INVALID_ARGS: |fd| is not a valid file descriptor.
//
// ZX_ERR_UNAVAILABLE: |fd| is busy or has been dup'ed and therefore is
// referenced by multiple entries in the file descriptor table.
//
// ZX_ERR_NOT_SUPPORTED: |fd| cannot be represented as a |zx_handle_t|.
//
// ZX_ERR_BAD_STATE: |fd| cannot be transferred to another process in its
// current state.
//
// ZX_ERR_ACCESS_DENIED: |fd| has insufficient rights to clone the underlying
// object.
zx_status_t fdio_fd_transfer(int fd, zx_handle_t* out_handle);

// Prepares a file descriptor for transfer, or falls-back to cloning it if
// the descriptor is not transferrable (e.g. it is an FD that has been dup()ed).
//
// Upon return, the given file descriptor has been removed from the file
// descriptor table for this process.
//
// Upon success, |out_handle| contains a handle that represents the given file
// descriptor.
//
// # Errors
//
// ZX_ERR_INVALID_ARGS: |fd| is not a valid file descriptor.
//
// ZX_ERR_UNAVAILABLE: |fd| is busy.
//
// ZX_ERR_NOT_SUPPORTED: |fd| cannot be represented as a |zx_handle_t|.
//
// ZX_ERR_BAD_STATE: |fd| cannot be transferred to another process in its
// current state.
//
// ZX_ERR_ACCESS_DENIED: |fd| has insufficient rights to clone the underlying
// object.
zx_status_t fdio_fd_transfer_or_clone(int fd, zx_handle_t* out_handle);

__END_CDECLS

#endif  // LIB_FDIO_INCLUDE_LIB_FDIO_FD_H_
