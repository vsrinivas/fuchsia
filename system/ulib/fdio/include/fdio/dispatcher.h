// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>
#include <zircon/compiler.h>
#include <fdio/remoteio.h>

__BEGIN_CDECLS

typedef struct fdio_dispatcher fdio_dispatcher_t;

typedef zx_status_t (*fdio_dispatcher_cb_t)(zx_handle_t h, void* func, void* cookie);

// Create a dispatcher that will process messages from many channels.
//
// The provided handler will be called when a handle is readable, and passed
// the cb and cookie pointers that are associated with that handle.
//
// If the remote side of the channel is closed, the handler will be
// called and passed a zero handle.
//
// A non-zero return will cause the handle to be closed.  If the non-zero
// return is *negative*, the handler will be called one last time, as if
// the channel had been closed remotely (zero handle).
zx_status_t fdio_dispatcher_create(fdio_dispatcher_t** out, fdio_dispatcher_cb_t cb);

// create a thread for a dispatcher and start it running
zx_status_t fdio_dispatcher_start(fdio_dispatcher_t* md, const char* name);

// run the dispatcher loop on the current thread, never to return
void fdio_dispatcher_run(fdio_dispatcher_t* md);

// add a channel to the dispatcher, using the default callback
zx_status_t fdio_dispatcher_add(fdio_dispatcher_t* md, zx_handle_t h,
                                void* func, void* cookie);

// add a channel to the dispatcher, using a specified callback
zx_status_t fdio_dispatcher_add_etc(fdio_dispatcher_t* md, zx_handle_t h,
                                    fdio_dispatcher_cb_t callback,
                                    void* func, void* cookie);

__END_CDECLS
