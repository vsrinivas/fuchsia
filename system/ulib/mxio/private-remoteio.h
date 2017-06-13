// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "private.h"

typedef struct mxrio mxrio_t;
struct mxrio {
    // base mxio io object
    mxio_t io;

    // channel handle for rpc
    mx_handle_t h;

    // event handle for device state signals, or socket handle
    mx_handle_t h2;

    // transaction id used for synchronous remoteio calls
    _Atomic mx_txid_t txid;
};

// These are for the benefit of namespace.c
// which needs lower level access to remoteio internals

// open operation directly on remoteio handle
mx_status_t mxrio_open_handle(mx_handle_t h, const char* path, int32_t flags,
                              uint32_t mode, mxio_t** out);

// open operation directly on remoteio handle
// returns new remoteio handle on success
// fails and discards non-REMOTE protocols
mx_status_t mxrio_open_handle_raw(mx_handle_t h, const char* path, int32_t flags,
                                  uint32_t mode, mx_handle_t *out);

// open operation directly on remoteio mxio_t
mx_status_t mxrio_open(mxio_t* io, const char* path, int32_t flags,
                       uint32_t mode, mxio_t** out);

// misc operation directly on remoteio mxio_t
mx_status_t mxrio_misc(mxio_t* io, uint32_t op, int64_t off,
                       uint32_t maxreply, void* ptr, size_t len);


// Shared with remotesocket.c

mx_status_t mxrio_close(mxio_t* io);

ssize_t mxrio_ioctl(mxio_t* io, uint32_t op, const void* in_buf,
                    size_t in_len, void* out_buf, size_t out_len);

mx_status_t mxrio_getobject(mx_handle_t rio_h, uint32_t op, const char* name,
                            int32_t flags, uint32_t mode,
                            mxrio_object_t* info);
