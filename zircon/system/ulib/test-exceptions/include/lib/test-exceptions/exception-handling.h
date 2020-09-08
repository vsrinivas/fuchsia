// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_TEST_EXCEPTIONS_EXCEPTION_HANDLING_H_
#define LIB_TEST_EXCEPTIONS_EXCEPTION_HANDLING_H_

#include <lib/zx/exception.h>

namespace test_exceptions {

// The functions below will extract the thread for an exception and manipulate the thread
// pointers so the thread is resumed on an exiting function. This is mostly used in death
// tests, where the test is checking that a thread hits an expected exception.
// Calling an exiting function means that the thread will have its thread exit handling
// functions called, and the thread's stack will be freed. Anything the thread allocated on
// the heap is leaked.
// NOTE: These functions will only work on an exception in the same process as where this
// function is called.

// This function points the thread to `zx_thread_exit`. Calling `zx_thread_exit` does not
// call any thread exit callback functions.
zx_status_t ExitExceptionZxThread(zx::exception exception);

// This function points the thread to `thrd_exit(0)`. This will call thrd exit callback
// functions and free the thread's stack.
// NOTE: Should only be called on thrd threads.
zx_status_t ExitExceptionCThread(zx::exception exception);

// This function points the thread to `pthread_exit(nullptr)`. This will call pthread exit callback
// functions and free the thread's stack.
// NOTE: Should only be called on pthread threads.
zx_status_t ExitExceptionPThread(zx::exception exception);

}  // namespace test_exceptions

#endif  // LIB_TEST_EXCEPTIONS_EXCEPTION_HANDLING_H_
