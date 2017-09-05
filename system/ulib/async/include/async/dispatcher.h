// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/syscalls/port.h>
#include <magenta/types.h>

__BEGIN_CDECLS

// Dispatcher interface for performing asynchronous operations.
// There may be multiple implementations of this interface.
typedef struct async_dispatcher async_t;

// Forward declarations for asynchronous operation structures.
typedef struct async_wait async_wait_t;
typedef struct async_task async_task_t;
typedef struct async_receiver async_receiver_t;

// Private state owned by the asynchronous dispatcher.
// Clients should initialize the contents of this structure to zero using
// |ASYNC_STATE_INIT| or with calloc, memset, or a similar means.
typedef struct {
    uintptr_t reserved[2];
} async_state_t;

#define ASYNC_STATE_INIT \
    { 0u, 0u }

// Flags for asynchronous operations.
enum {
    // Asks the dispatcher to notify the handler when the dispatcher itself
    // is being shut down so that the handler can release its resources.
    //
    // The dispatcher will invoke the handler with a status of
    // |MX_ERR_CANCELED| to indicate that it is being shut down.
    //
    // This flag only applies to pending waits and tasks; receivers will
    // not be notified of shutdown.
    ASYNC_FLAG_HANDLE_SHUTDOWN = 1 << 0,
};

// Asynchronous dispatcher interface.
//
// Clients should prefer using the |async_*| inline functions declared in the
// other header files.  See the documentation of those inline functions for
// details about each method's purpose and behavior.
//
// This interface consists of three groups of methods:
//
// - Waiting for signals: |begin_wait|, |cancel_wait|
// - Posting tasks: |post_task|, |cancel_task|
// - Queuing packets: |queue_packet|
//
// Implementations of this interface are not required to support all of these methods.
// Unsupported methods must have valid (non-null) function pointers, must have
// no side-effects, and must return |MX_ERR_NOT_SUPPORTED| when called.
// Furthermore, if an implementation supports one method of a group, such as |begin_wait|,
// it must also support the other methods of the group, such as |cancel_wait|.
//
// Many clients assume that the dispatcher interface is fully implemented and may
// fail to work with dispatchers that do not support the methods they need.
// Therefore general-purpose dispatcher implementations are strongly encouraged to
// support the whole interface to ensure broad compatibility.
typedef struct async_ops {
    mx_status_t (*begin_wait)(async_t* async, async_wait_t* wait);
    mx_status_t (*cancel_wait)(async_t* async, async_wait_t* wait);
    mx_status_t (*post_task)(async_t* async, async_task_t* task);
    mx_status_t (*cancel_task)(async_t* async, async_task_t* task);
    mx_status_t (*queue_packet)(async_t* async, async_receiver_t* receiver,
                                const mx_packet_user_t* data);
} async_ops_t;
struct async_dispatcher {
    const async_ops_t* ops;
};

__END_CDECLS
