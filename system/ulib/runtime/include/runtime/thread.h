// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <stdint.h>
#include <system/compiler.h>

__BEGIN_CDECLS

typedef intptr_t (*mxr_thread_entry_t)(void*);

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
mx_status_t mxr_thread_join(mxr_thread_t* thread, intptr_t* return_value_out);

// If a thread is detached, instead of waiting to be joined, it will
// clean up after itself, and the return value of the thread's
// entrypoint is ignored.
mx_status_t mxr_thread_detach(mxr_thread_t* thread);

// Exit from the thread. Equivalent to returning return_value from
// that thread's entrypoint.
_Noreturn void mxr_thread_exit(mxr_thread_t* thread, intptr_t return_value);

// Get the mx_handle_t corresponding to the given thread.
// WARNING:
// This is intended for debuggers and so on. Holding this wrong could
// break internal invariants of mxr_thread_t. It is inherently
// racy. You probably don't need this.
mx_handle_t mxr_thread_get_handle(mxr_thread_t* thread);

// A private entrypoint into mxruntime initialization.
mxr_thread_t* __mxr_thread_main(void);

#pragma GCC visibility pop

__END_CDECLS
