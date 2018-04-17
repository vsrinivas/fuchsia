// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Dispatcher interface for performing asynchronous operations.
// There may be multiple implementations of this interface.
typedef struct async_dispatcher async_t;

// Forward declarations for asynchronous operation structures.
typedef struct async_guest_bell_trap async_guest_bell_trap_t;
typedef struct async_wait async_wait_t;
typedef struct async_task async_task_t;
typedef struct async_receiver async_receiver_t;

// Private state owned by the asynchronous dispatcher.
// This allows the dispatcher to associate a small amount of state with pending
// asynchronous operations without having to allocate additional heap storage of
// its own.
//
// Clients must initialize the contents of this structure to zero using
// |ASYNC_STATE_INIT| or with calloc, memset, or a similar means.
typedef struct {
    uintptr_t reserved[2];
} async_state_t;

#define ASYNC_STATE_INIT \
    { 0u, 0u }

// Asynchronous dispatcher interface.
//
// Clients should not call into this interface directly: use the wrapper functions
// declared in other header files, such as |async_begin_wait()| in <lib/async/wait.h>.
// See the documentation of those functions for details about each method's purpose
// and behavior.
//
// This interface consists of several groups of methods:
//
// - Timing: |now|
// - Waiting for signals: |begin_wait|, |cancel_wait|
// - Posting tasks: |post_task|, |cancel_task|
// - Queuing packets: |queue_packet|
// - Virtual machine operations: |set_guest_bell_trap|
//
// To preserve binary compatibility, each successive version of this interface
// is guaranteed to be backwards-compatible with clients of earlier versions.
// New methods must only be added by extending the structure at the end and
// declaring a new version number.  Do not reorder the declarations or modify
// existing versions.
//
// Implementations of this interface must provide valid (non-null) function pointers
// for every method declared in the interface version they support.  Unsupported
// methods must return |ZX_ERR_NOT_SUPPORTED| and have no other side-effects.
// Furthermore, if an implementation supports one method of a group, such as |begin_wait|,
// then it must also support the other methods of the group, such as |cancel_wait|.
//
// Many clients assume that the dispatcher interface is fully implemented and may
// fail to work with dispatchers that do not support the methods they need.
// Therefore general-purpose dispatcher implementations are encouraged to support
// the whole interface to ensure broad compatibility.
enum {
    ASYNC_OPS_V1 = 1,
};
typedef struct async_ops {
    // The interface version number, e.g. |ASYNC_OPS_V1|.
    uint32_t version;

    // Reserved for future expansion, set to zero.
    uint32_t reserved;

    // Operations supported by |ASYNC_OPS_V1|.
    struct v1 {
        // See |async_now()| for details.
        zx_time_t (*now)(async_t* async);
        // See |async_begin_wait()| for details.
        zx_status_t (*begin_wait)(async_t* async, async_wait_t* wait);
        // See |async_cancel_wait()| for details.
        zx_status_t (*cancel_wait)(async_t* async, async_wait_t* wait);
        // See |async_post_task()| for details.
        zx_status_t (*post_task)(async_t* async, async_task_t* task);
        // See |async_cancel_task()| for details.
        zx_status_t (*cancel_task)(async_t* async, async_task_t* task);
        // See |async_queue_packet()| for details.
        zx_status_t (*queue_packet)(async_t* async, async_receiver_t* receiver,
                                    const zx_packet_user_t* data);
        // See |async_set_guest_bell_trap()| for details.
        zx_status_t (*set_guest_bell_trap)(async_t* async, async_guest_bell_trap_t* trap,
                                           zx_handle_t guest, zx_vaddr_t addr, size_t length);
    } v1;
} async_ops_t;
struct async_dispatcher {
    const async_ops_t* ops;
};

__END_CDECLS
