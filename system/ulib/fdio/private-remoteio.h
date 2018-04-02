// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "private.h"

typedef struct zxrio zxrio_t;
struct zxrio {
    // base fdio io object
    fdio_t io;

    // channel handle for rpc
    zx_handle_t h;

    // event handle for device state signals, or socket handle
    zx_handle_t h2;

    // transaction id used for synchronous remoteio calls
    _Atomic zx_txid_t txid;
};

// These are for the benefit of namespace.c
// which needs lower level access to remoteio internals

// open operation directly on remoteio handle
zx_status_t zxrio_open_handle(zx_handle_t h, const char* path, uint32_t flags,
                              uint32_t mode, fdio_t** out);

// open operation directly on remoteio handle
// returns new remoteio handle on success
// fails and discards non-REMOTE protocols
zx_status_t zxrio_open_handle_raw(zx_handle_t h, const char* path, uint32_t flags,
                                  uint32_t mode, zx_handle_t *out);

// open operation directly on remoteio fdio_t
zx_status_t zxrio_open(fdio_t* io, const char* path, uint32_t flags,
                       uint32_t mode, fdio_t** out);

// misc operation directly on remoteio fdio_t
zx_status_t zxrio_misc(fdio_t* io, uint32_t op, int64_t off,
                       uint32_t maxreply, void* ptr, size_t len);


// Shared with remotesocket.c

zx_status_t zxrio_close(fdio_t* io);

ssize_t zxrio_ioctl(fdio_t* io, uint32_t op, const void* in_buf,
                    size_t in_len, void* out_buf, size_t out_len);

// Acquires a new connection to an object.
//
// Returns a description of the opened object in |info|, and
// the control channel to the object in |out|.
//
// |info| may contain an additional handle.
zx_status_t zxrio_getobject(zx_handle_t rio_h, uint32_t op, const char* name,
                            uint32_t flags, uint32_t mode,
                            zxrio_describe_t* info, zx_handle_t* out);

// Acquire the additional handle from |info|.
//
// Returns |ZX_OK| if a handle was returned.
// Returns |ZX_ERR_NOT_FOUND| if no handle can be returned.
zx_status_t zxrio_object_extract_handle(const zxrio_object_info_t* info,
                                        zx_handle_t* out);

// Create a fdio (if possible) from handles and info.
//
// The Control channel is provided in |handle|, and auxillary
// handles may be provided in the |info| object.
//
// This function always takes control of all handles.
// They are transferred into the |out| object on success,
// or closed on failure.
zx_status_t fdio_from_handles(zx_handle_t handle, zxrio_object_info_t* info,
                              fdio_t** out);

// Wait/Read from a new client connection, with the expectation of
// acquiring an Open response.
//
// Shared implementation between RemoteIO and FIDL, since the response
// message is aligned.
//
// Does not close |h|, even on error.
zx_status_t zxrio_process_open_response(zx_handle_t h, zxrio_describe_t* info);
