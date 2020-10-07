// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_INCLUDE_LIB_FDIO_FDIO_H_
#define LIB_FDIO_INCLUDE_LIB_FDIO_FDIO_H_

#include <stdint.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// An opaque structure that represents the object to which a file descriptor
// refers.
typedef struct fdio fdio_t;

// Creates an |fdio_t| from a |zx_handle_t|.
//
// The |handle| must be to a channel, socket, vmo, or debuglog object.
//
// If the |zx_handle_t| is a channel, then the channel must implement the
// |fuchsia.io.Node| protocol.
//
// Always consumes |handle|.
//
// # Errors
//
// TODO: Catalog errors.
zx_status_t fdio_create(zx_handle_t handle, fdio_t** out_io);

// Creates an |fdio_t| that does nothing.
fdio_t* fdio_null_create(void);

// Access the |zxio_t| field within an |fdio_t|.
typedef struct zxio_tag zxio_t;
zxio_t* fdio_get_zxio(fdio_t* io);

// Creates a file descriptor that does nothing.
//
// Returns -1 and sets errno if the function is unable to create the file
// descriptor (e.g., if the file descriptor table is full).
int fdio_fd_create_null(void);

// Install an |fdio_t| in the file descriptor table for this process.
//
// If fd >= 0, request a specific fd, and starting_fd is ignored.
//
// If fd < 0, request the first available fd >= starting_fd.
//
// Upon success, returns the allocated file descriptor. Returns -1 on failure.
//
// Upon success, takes ownership of the given reference to the |fdio_t|. That
// reference is owned by the file descriptor table. Upon failure, the caller
// retains ownership of that reference. Specifically, the caller is responsible
// for calling |fdio_unsafe_release| upon failure.
//
// TODO(REVIEW): This function should always take ownership of the given
// |fdio_t| reference.
int fdio_bind_to_fd(fdio_t* io, int fd, int starting_fd);

// Removes a file descriptor from the file descriptor table for this process.
//
// Upon success, the |fdio_t| underlying the file descriptor is returned in
// |io_out|, and the caller receives ownership of one reference to |fdio_t|.
// Specifically, the caller is responsible for calling |fdio_unsafe_release|
// upon success.
//
// Upon failure, the file descriptor is not removed from the file descriptor
// table for this process.
//
// TODO(REVIEW): This function should always consume the file descriptor.
//
// # Errors
//
// ZX_ERR_INVALID_ARGS: |fd| is not a valid file descriptor.
//
// ZX_ERR_UNAVAILABLE: |fd| is busy or has been dup'ed and therefore is
// referenced by multiple entries in the file descriptor table.
zx_status_t fdio_unbind_from_fd(int fd, fdio_t** io_out);

// If this fd represents a "service" (an rpc channel speaking
// an unknown fidl protocol or a fuchsia.io.* protocol),
// this call will return ZX_OK and return the underlying handle.
// On both success and failure, the fd is effectively closed.
//
// ZX_ERR_NOT_SUPPORTED is returned if this fd does not represent
// a FIDL transport
//
// ZX_ERR_UNAVAILABLE is returned if this fd has been dup()'d and
// duplicates are sharing the FIDL transport
//
// TODO: Can also return ZX_ERR_NOT_FOUND. Maybe should be ZX_ERR_INVALID_ARGS?
// TODO: This function appears to work only for |fuchsia.io| protocols now.
// Should we rename it to something like |fdio_take_remote|?
zx_status_t fdio_get_service_handle(int fd, zx_handle_t* out);

// Storage for a ZXIO object.
//
// See <lib/zxio/ops.h> for more information.
typedef struct zxio_storage zxio_storage_t;

// Creates an |fdio_t| that is backed by a |zxio_t|.
//
// The |zxio_t| is initialized with a null ops table. The |zxio_storage_t| for
// the |zxio_t| is returned via |out_storage|. The client can re-initialize the
// |zxio_storage_t| to customize the behavior of the |zxio_t|.
//
// The returned |zxio_storage_t| is valid for the lifetime of the returned
// |fdio_t|.
//
// To bind the |fdio_t| to a file descriptor, use |fdio_bind_to_fd|.
//
// Upon success, the caller receives ownership of one reference to |fdio_t|.
// Specifically, the caller is responsible for calling |fdio_unsafe_release|
// upon success.
//
// Upon failure, returns NULL.
fdio_t* fdio_zxio_create(zxio_storage_t** out_storage);

__END_CDECLS

#endif  // LIB_FDIO_INCLUDE_LIB_FDIO_FDIO_H_
