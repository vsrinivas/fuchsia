// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/compiler.h>
#include <mxio/io.h>
#include <stdint.h>

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

void bootfs_parse(void* _data, size_t len,
                  void (*cb)(void*, const char* fn, size_t off, size_t len),
                  void* cb_arg);


// used for bootstrap
void mxio_install_root(mxio_t* root);

// attempt to install a mxio in the unistd fd table
// if fd >= 0, request a specific fd, and starting_fd is ignored
// if fd < 0, request the first available fd >= starting_fd
// returns fd on success
// the mxio must have been upref'd on behalf of the fdtab first
int mxio_bind_to_fd(mxio_t* io, int fd, int starting_fd);

// creates a do-nothing mxio_t
mxio_t* mxio_null_create(void);

// wraps a message port with an mxio_t using remote io
mxio_t* mxio_remote_create(mx_handle_t h, mx_handle_t e);

// creates a mxio that wraps a log object
// this will allocate a per-thread buffer (on demand) to assemble
// entire log-lines and flush them on newline or buffer full.
mxio_t* mxio_logger_create(mx_handle_t);

// Type of the hook for mxio_loader_service.  The first argument is
// the one passed to mxio_loader_service, and the second is the file
// name passed to dlopen or found in a DT_NEEDED entry.
typedef mx_handle_t (*mxio_loader_service_function_t)(void* loader_arg,
                                                      const char* file);

// Start a thread to resolve loader service requests and return a
// message pipe handle to talk to said service.  If the function
// passed is NULL, a default implementation that reads from the
// filesystem is used.
mx_handle_t mxio_loader_service(mxio_loader_service_function_t loader,
                                void* loader_arg);

// Examine the set of handles received at process startup for one matching
// the given id.  If one is found, return it and remove it from the set
// available to future calls.
mx_handle_t mxio_get_startup_handle(uint32_t id);


__END_CDECLS
