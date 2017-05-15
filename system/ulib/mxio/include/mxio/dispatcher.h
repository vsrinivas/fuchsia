// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS

typedef struct mxio_dispatcher mxio_dispatcher_t;

typedef mx_status_t (*mxio_dispatcher_cb_t)(mx_handle_t h, void* func, void* cookie);

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
mx_status_t mxio_dispatcher_create(mxio_dispatcher_t** out, mxio_dispatcher_cb_t cb);

// create a thread for a dispatcher and start it running
mx_status_t mxio_dispatcher_start(mxio_dispatcher_t* md, const char* name);

// run the dispatcher loop on the current thread, never to return
void mxio_dispatcher_run(mxio_dispatcher_t* md);

// add a channel to the dispatcher, using the default callback
mx_status_t mxio_dispatcher_add(mxio_dispatcher_t* md, mx_handle_t h,
                                void* func, void* cookie);

// add a channel to the dispatcher, using a specified callback
mx_status_t mxio_dispatcher_add_etc(mxio_dispatcher_t* md, mx_handle_t h,
                                    mxio_dispatcher_cb_t callback,
                                    void* func, void* cookie);

// dispatcher callback return code that there were no messages to read
#define ERR_DISPATCHER_NO_WORK ERR_SHOULD_WAIT

// indicates message handed off to another server
// used by rio remote handler for deferred reply pipe completion
#define ERR_DISPATCHER_INDIRECT ERR_NEXT

// indicates that this was a close message and that no further
// callbacks should be made to the dispatcher
#define ERR_DISPATCHER_DONE ERR_STOP

__END_CDECLS
