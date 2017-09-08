// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/compiler.h>
#include <stdint.h>
#include <unistd.h>

__BEGIN_CDECLS

// These routines are "internal" to mxio but used by some companion
// code like userboot and devmgr

typedef struct mxio mxio_t;

// Utilities to help assemble handles for a new process
// may return up to MXIO_MAX_HANDLES
mx_status_t mxio_clone_root(mx_handle_t* handles, uint32_t* types);
mx_status_t mxio_clone_cwd(mx_handle_t* handles, uint32_t* types);
mx_status_t mxio_clone_fd(int fd, int newfd, mx_handle_t* handles, uint32_t* types);
mx_status_t mxio_pipe_pair_raw(mx_handle_t* handles, uint32_t* types);
mx_status_t mxio_transfer_fd(int fd, int newfd, mx_handle_t* handles, uint32_t* types);

// Attempt to create an mxio fd from some handles and their associated types,
// as returned from mxio_transfer_fd.
//
// Can only create fds around:
// - Remote IO objects
// - Pipes
// - Connected sockets
//
// This function transfers ownership of handles to the fd on success, and
// closes them on failure.
mx_status_t mxio_create_fd(mx_handle_t* handles, uint32_t* types, size_t hcount, int* fd_out);

typedef struct bootfs_entry bootfs_entry_t;

typedef struct bootfs {
    mx_handle_t vmo;
    uint32_t dirsize;
    void* dir;
} bootfs_t;

mx_status_t bootfs_create(bootfs_t* bfs, mx_handle_t vmo);
void bootfs_destroy(bootfs_t* bfs);
mx_status_t bootfs_open(bootfs_t* bfs, const char* name, mx_handle_t* vmo);
mx_status_t bootfs_parse(bootfs_t* bfs,
                         mx_status_t (*cb)(void* cookie, const bootfs_entry_t* entry),
                         void* cookie);

// used for bootstrap
void mxio_install_root(mxio_t* root);

// attempt to install a mxio in the unistd fd table
// if fd >= 0, request a specific fd, and starting_fd is ignored
// if fd < 0, request the first available fd >= starting_fd
// returns fd on success
// the mxio must have been upref'd on behalf of the fdtab first
int mxio_bind_to_fd(mxio_t* io, int fd, int starting_fd);

// attempt to detach an mxio_t from the fd table
// returns MX_ERR_INVALID_ARGS if fd is out of range or doesn't exist
// returns MX_ERR_UNAVAILABLE if the fd is busy or has been dup'd
// returns mxio_t via io_out with refcount 1 on success
mx_status_t mxio_unbind_from_fd(int fd, mxio_t** io_out);

// If this fd represents a "service" (an rpc channel speaking
// a non-mxio protocol), this call will return MX_OK and
// return the underlying handle.
// On both success and failure, the fd is effectively closed.
mx_status_t mxio_get_service_handle(int fd, mx_handle_t* out);

// creates a do-nothing mxio_t
mxio_t* mxio_null_create(void);

// Wraps a channel with an mxio_t using remote io.
// Takes ownership of h and e.
mxio_t* mxio_remote_create(mx_handle_t h, mx_handle_t e);

// Wraps a channel with an mxio_t using an unknown rpc protocl.
// Takes ownership of h.
mxio_t* mxio_service_create(mx_handle_t);

// creates a mxio that wraps a log object
// this will allocate a per-thread buffer (on demand) to assemble
// entire log-lines and flush them on newline or buffer full.
mxio_t* mxio_logger_create(mx_handle_t);

// create a mxio that wraps a function
// used for plumbing stdout/err to logging subsystems, etc
mxio_t* mxio_output_create(ssize_t (*func)(void* cookie, const void* data, size_t len),
                           void* cookie);

// Attempt to connect a channel to a named service.
// On success the channel is connected.  On failure
// an error is returned and the handle is closed.
mx_status_t mxio_service_connect(const char* svcpath, mx_handle_t h);

// Attempt to connect a channel to a named service relative to dir.
// On success the channel is connected.  On failure
// an error is returned and the handle is closed.
mx_status_t mxio_service_connect_at(mx_handle_t dir, const char* path, mx_handle_t h);

// Attempt to clone a sevice handle by doing a pipelined
// CLONE operation, returning the new channel endpoint,
// or MX_HANDLE_INVALID.
mx_handle_t mxio_service_clone(mx_handle_t h);

__END_CDECLS
