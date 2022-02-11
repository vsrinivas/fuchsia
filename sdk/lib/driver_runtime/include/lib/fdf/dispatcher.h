// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_DISPATCHER_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_DISPATCHER_H_

#include <lib/async/dispatcher.h>
#include <lib/fdf/types.h>

__BEGIN_CDECLS

// Usage Notes:
//
// fdf_dispatcher_t can be used in conjunction with a fdf_channel_t
// to initiate asynchronous read operations. The dispatcher is in
// charge of dispatching the read callbacks.
//
// Example:
//
//  struct dispatcher_destructed_observer {
//    fdf_dispatcher_destructed_observer_t fdf_observer;
//    my_ctx* ctx;
//  };
//
//  void driver_start() {
//    TODO(fxb/85946): update this once scheduler_role is supported.
//    const char* scheduler_role = "";
//    struct dispatcher_destructed_observer observer = {
//      ...
//    };
//
//    fdf_dispatcher_t* dispatcher;
//    fdf_status_t status =
//        fdf_dispatcher_create(0, scheduler_role, strlen(scheduler_role),
//                              &observer->fdf_observer, &dispatcher);
//
//    fdf_channel_read_t channel_read;
//    ...
//    status = fdf_channel_wait_async(dispatcher, channel_read, 0);
//
//    // The dispatcher will call the channel_read handler when ready.
//
//    // This begins the dispatcher destruction process.
//    fdf_dispatcher_destroy_async(dispatcher);
// }
//
typedef struct fdf_dispatcher fdf_dispatcher_t;

typedef struct fdf_dispatcher_destructed_observer fdf_dispatcher_destructed_observer_t;

// Called when the dispatcher's asynchronous destruction has completed.
typedef void(fdf_dispatcher_destructed_handler_t)(fdf_dispatcher_destructed_observer_t* observer);

// Holds context for the observer which will be called when the dispatcher's
// asynchronous destruction has completed.
//
// After creating the dispatcher, the client is responsible for retaining
// this structure in memory (and unmodified) until the handler runs.
struct fdf_dispatcher_destructed_observer {
  fdf_dispatcher_destructed_handler_t* handler;
};

// This flag allows parallel calls into callbacks set in the dispatcher.
// Cannot be set in conjunction with FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS.
#define FDF_DISPATCHER_OPTION_UNSYNCHRONIZED (1 << 0)
// This flag indicates that the dispatcher may not share zircon threads with other drivers.
// Cannot be set in conjunction with FDF_DISPATCHER_OPTION_UNSYNCHRONIZED.
#define FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS (1 << 1)

// |scheduler_role| is a hint. It may or not impact the priority the work scheduler against the
// dispatcher is handled at. It may or may not impact the ability for other drivers to share zircon
// threads with the dispatcher.
// |scheduler_role_len | is the length of the string, without including the terminating
// NULL character.
// TODO(fxb/85946): currently |scheduler_role| is not implemented.
// |observer| will be called after |fdf_dispatcher_destroy_async| has been called,
// and the dispatcher has completed its asynchronous destruction.
//
// This must be called from a thread managed by the driver runtime.
fdf_status_t fdf_dispatcher_create(uint32_t options, const char* scheduler_role,
                                   size_t scheduler_role_len,
                                   fdf_dispatcher_destructed_observer_t* observer,
                                   fdf_dispatcher_t** dispatcher);

// Returns the asynchronous dispatch interface.
async_dispatcher_t* fdf_dispatcher_get_async_dispatcher(fdf_dispatcher_t* dispatcher);

// Returns an unowned dispatcher provided an async dispatcher. If |async_dispatcher| was not
// retrieved via `fdf_dispatcher_get_async_dispatcher`, the call will result in a crash.
fdf_dispatcher_t* fdf_dispatcher_from_async_dispatcher(async_dispatcher_t* async_dispatcher);

// Returns the options set for this dispatcher.
uint32_t fdf_dispatcher_get_options(fdf_dispatcher_t* dispatcher);

// Destroying a dispatcher is an asynchronous operation.
//
// Once |fdf_dispatcher_destroy_async| is called, the dispatcher will no longer
// accept queueing new async_dispatcher_t operations or ChannelRead callbacks.
//
// The dispatcher will asynchronously wait for all pending async_dispatcher_t
// and ChannelRead callbacks to complete. Then it will serially cancel all
// remaining callbacks with ZX_ERR_CANCELED and call the destroy handler set
// in |fdf_dispatcher_create|.
//
// It is safe to call this from a callback started by this dispatcher.
void fdf_dispatcher_destroy_async(fdf_dispatcher_t* dispatcher);

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_DISPATCHER_H_
