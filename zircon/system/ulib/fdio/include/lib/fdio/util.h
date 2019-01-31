// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>
#include <zircon/compiler.h>
#include <stdint.h>
#include <unistd.h>

__BEGIN_CDECLS

// These routines are "internal" to fdio but used by some companion
// code like userboot and devmgr

typedef struct fdio fdio_t;

// Utilities to help assemble handles for a new process
// may return up to FDIO_MAX_HANDLES
zx_status_t fdio_clone_cwd(zx_handle_t* handles, uint32_t* types);
zx_status_t fdio_clone_fd(int fd, int newfd, zx_handle_t* handles, uint32_t* types);
zx_status_t fdio_transfer_fd(int fd, int newfd, zx_handle_t* handles, uint32_t* types);

// Attempt to create an fdio fd from some handles and their associated types,
// as returned from fdio_transfer_fd.
//
// Can only create fds around:
// - Remote IO objects
// - Pipes
// - Connected sockets
//
// This function transfers ownership of handles to the fd on success, and
// closes them on failure.
zx_status_t fdio_create_fd(zx_handle_t* handles, uint32_t* types, size_t hcount, int* fd_out);

// attempt to install a fdio in the unistd fd table
// if fd >= 0, request a specific fd, and starting_fd is ignored
// if fd < 0, request the first available fd >= starting_fd
// returns fd on success
// the fdio must have been upref'd on behalf of the fdtab first
int fdio_bind_to_fd(fdio_t* io, int fd, int starting_fd);

// attempt to detach an fdio_t from the fd table
// returns ZX_ERR_INVALID_ARGS if fd is out of range or doesn't exist
// returns ZX_ERR_UNAVAILABLE if the fd is busy or has been dup'd
// returns fdio_t via io_out with refcount 1 on success
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
zx_status_t fdio_get_service_handle(int fd, zx_handle_t* out);

// creates a do-nothing fdio_t
fdio_t* fdio_null_create(void);

// Wraps a channel with an fdio_t using remote io.
// Takes ownership of h and e.
fdio_t* fdio_remote_create(zx_handle_t h, zx_handle_t event);

// creates a fdio that wraps a log object
// this will allocate a per-thread buffer (on demand) to assemble
// entire log-lines and flush them on newline or buffer full.
fdio_t* fdio_logger_create(zx_handle_t);

typedef struct zxio_storage zxio_storage_t;

// Creates an |fdio_t| that is backed by a |zxio_t|.
//
// The |zxio_t| is initialized with a null ops table. The |zxio_storage_t| for
// the |zxio_t| is returned via |out_storage|. The client can re-initialize the
// |zxio_storage_t| to customize the behavior of the |zxio_t|.
//
// To bind the |fdio_t| to a file descriptor, use |fdio_bind_to_fd|.
//
// Upon failure, returns NULL.
fdio_t* fdio_zxio_create(zxio_storage_t** out_storage);

// Attempt to connect a channel to a named service.
// On success the channel is connected.  On failure
// an error is returned and the handle is closed.
zx_status_t fdio_service_connect(const char* svcpath, zx_handle_t h);

// Attempt to connect a channel to a named service relative to dir.
// On success the channel is connected.  On failure
// an error is returned and the handle is closed.
zx_status_t fdio_service_connect_at(zx_handle_t dir, const char* path, zx_handle_t h);

// Same as |fdio_service_connect|, but allows the passing of flags.
zx_status_t fdio_open(const char* path, uint32_t zxflags, zx_handle_t h);

// Same as |fdio_service_connect_at, but allows the passing of flags.
zx_status_t fdio_open_at(zx_handle_t dir, const char* path, uint32_t zxflags, zx_handle_t h);

// Attempt to clone a service handle by doing a pipelined
// CLONE operation, returning the new channel endpoint,
// or ZX_HANDLE_INVALID.
zx_handle_t fdio_service_clone(zx_handle_t h);

// Attempt to clone a service handle by doing a pipelined
// CLONE operation, using the provided serving channel.
// On success srv is bound to a clone of h.  On failure
// an error is returned and srv is closed.
// Takes ownership of srv.
zx_status_t fdio_service_clone_to(zx_handle_t h, zx_handle_t srv);

__END_CDECLS
