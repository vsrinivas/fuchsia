// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_C_WAITER_ASYNC_WAITER_H_
#define LIB_FIDL_C_WAITER_ASYNC_WAITER_H_

#include <zircon/types.h>
#include <stdint.h>

typedef uint64_t FidlAsyncWaitID;
static_assert(sizeof(uintptr_t) <= sizeof(uint64_t),
              "uintptr_t larger than uint64_t!");

typedef void (*FidlAsyncWaitCallback)(zx_status_t result,
                                      zx_signals_t pending,
                                      uint64_t count,
                                      void* closure);

// Functions for asynchronously waiting (and cancelling asynchronous waits) on a
// handle.
//
// Thread-safety:
//   - |CancelWait(wait_id)| may only be called on the same thread as the
//     |AsyncWait()| that provided |wait_id| was called on.
//   - A given |FidlAsyncWaiter|'s functions may only be called on the thread(s)
//     that it is defined to be valid on (typically including the thread on
//     which the |FidlAsyncWaiter| was provided). E.g., a library may require
//     initialization with a single |FidlAsyncWaiter| and stipulate that it only
//     be used on threads on which that |FidlAsyncWaiter| is valid.
//   - If a |FidlAsyncWaiter| is valid on multiple threads, then its functions
//     must be thread-safe (subject to the first restriction above).
struct FidlAsyncWaiter {
  // Arranges for |callback| to be called on the current thread at some future
  // when |handle| satisfies |signals| or it is known that it will never satisfy
  // |signals| (with the same behavior as |zx_object_wait_one()|).
  //
  // |callback| will not be called in the nested context of |AsyncWait()|, but
  // only, e.g., from some run loop. |callback| is provided with the |closure|
  // argument as well as the result of the wait. For each call to |AsyncWait()|,
  // |callback| will be called at most once.
  //
  // |handle| must not be closed or transferred (via |zx_channel_write()| or
  // |zx_channel_call()|; this is equivalent to closing the handle) until either
  // the callback has been executed or the async wait has been cancelled using
  // the returned (nonzero) |FidlAsyncWaitID| (see |CancelWait()|). Otherwise,
  // an invalid (or, worse, re-used) handle may be waited on by the
  // implementation of this |FidlAsyncWaiter|.
  //
  // Note that once the callback has been called, the returned |FidlAsyncWaitID|
  // becomes invalid.
  FidlAsyncWaitID (*AsyncWait)(zx_handle_t handle,
                               zx_signals_t signals,
                               zx_time_t timeout,
                               FidlAsyncWaitCallback callback,
                               void* closure);

  // Cancels an outstanding async wait (specified by |wait_id|) initiated by
  // |AsyncWait()|. This may only be called from the same thread on which the
  // corresponding |AsyncWait()| was called, and may only be called if the
  // callback to |AsyncWait()| has not been called.
  //
  // Once this has been called, the callback provided to |AsyncWait()| will not
  // be called. Moreover, it is then immediately safe to close or transfer the
  // handle provided to |AsyncWait()|. (I.e., the implementation of this
  // |MojoAsyncWaiter| will no longer wait on, or do anything else with, the
  // handle.)
  void (*CancelWait)(FidlAsyncWaitID wait_id);
};

#endif  // LIB_FIDL_C_WAITER_ASYNC_WAITER_H_
