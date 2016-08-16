// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <system/compiler.h>

__BEGIN_CDECLS

typedef int (*mxr_thread_entry_t)(void*);

typedef struct mxr_thread mxr_thread_t;

#pragma GCC visibility push(hidden)

// Create and launch a thread. The thread, once constructed, will call
// entry(arg). If successful, the thread pointer is placed into
// thread_out, and NO_ERROR is returned. Otherwise a failure status is
// returned. The thread can return an integer value from entry. The
// thread should then be either joined or detached.
mx_status_t mxr_thread_create(mxr_thread_entry_t entry, void* arg, const char* name, mxr_thread_t** thread_out);

// Once created, threads can be either joined or detached. If a thread
// is joined or detached more than once, ERR_INVALID_ARGS is
// returned. Some of the resources allocated to a thread are not
// collected until it returns and it is either joined or detached.

// If a thread is joined, the caller of mxr_thread_join blocks until
// the other thread is finished running. The return value of the
// joined thread is placed in return_value_out if non-NULL.
mx_status_t mxr_thread_join(mxr_thread_t* thread, int* return_value_out);

// If a thread is detached, instead of waiting to be joined, it will
// clean up after itself, and the return value of the thread's
// entrypoint is ignored.
mx_status_t mxr_thread_detach(mxr_thread_t* thread);

// Get a magenta handle to the thread for debugging purposes, or to
// the current thread if NULL. Note that this is the same handle that
// mxr_thread_t uses internally, not a rights-restricted duplicate,
// and so care must be used to not violate invariants. This primarily
// means not closing the handle. In particular, anyone wanting to wait
// on a thread should mxr_thread_join it, not directly wait on this
// handle.
mx_handle_t mxr_thread_get_handle(mxr_thread_t* thread);

// A private entrypoint into mxruntime initialization.
void __mxr_thread_main(void);

#pragma GCC visibility pop

__END_CDECLS
