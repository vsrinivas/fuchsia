// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// This file is a collection of utilities for writing tests.
// Typically they are wrappers on system calls and other routines
// and save the caller from having to test the return code (for cases
// where there's no point in continuing with the test if the call fails).
// Note that if these calls fail they cause the process to exit, and
// are not intended to be used for tests that have multiple "subtests"
// and it's reasonable to continue with the other subtests if a syscall
// in one fails.

#include <stddef.h>
#include <threads.h>
#include <magenta/types.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS

// Basic watchdog/timeout duration.
#define TU_WATCHDOG_DURATION_SECONDS 2
#define TU_WATCHDOG_DURATION_NANOSECONDS ((int64_t) TU_WATCHDOG_DURATION_SECONDS * 1000 * 1000 * 1000)

void* tu_malloc(size_t size);

char* tu_strdup(const char* s);

// Print a message saying a syscall (or similar) function failed,
// and terminate the process.
// |what| is typically the name of the function that had the syscall failure,
// but it can include more descriptive text as desired.

void tu_fatal(const char *what, mx_status_t status);

// A wrapper on mx_handle_close.

void tu_handle_close(mx_handle_t handle);

// A wrapper on launchpad_launch.

mx_handle_t tu_launch(const char* name,
                      int argc, const char* const* argv,
                      const char* const* envp,
                      size_t num_handles, mx_handle_t* handles,
                      uint32_t* handle_ids);

// A wrapper on launchpad_launch_mxio_etc.

mx_handle_t tu_launch_mxio_etc(const char* name,
                               int argc, const char* const* argv,
                               const char* const* envp,
                               size_t num_handles, mx_handle_t* handles,
                               uint32_t* handle_ids);

// A wrapper on C11 thrd_create.

void tu_thread_create_c11(thrd_t* thread, thrd_start_t entry, void* arg,
                          const char* name);

// A wrapper on mx_msgpipe_create.
// For callers that have separate variables for each side of the pipe, this takes two pointers
// instead of an array of two handles that the syscall has.

void tu_message_pipe_create(mx_handle_t* handle0, mx_handle_t* handle1);

// A wrapper on mx_msgpipe_write.

void tu_message_write(mx_handle_t handle, const void* bytes, uint32_t num_bytes,
                      const mx_handle_t* handles, uint32_t num_handles, uint32_t flags);

// A wrapper on mx_msgpipe_read.

void tu_message_read(mx_handle_t handle, void* bytes, uint32_t* num_bytes,
                     mx_handle_t* handles, uint32_t* num_handles, uint32_t flags);

// Wait for |handle| to be readable.
// Returns true if the handle is readable, and false if the peer has closed its end.
// The call fails and the process terminates if the call times out within TU_WATCHDOG_DURATION_NANOSECONDS.
bool tu_wait_readable(mx_handle_t handle);

// Wait for |handle| to be signaled (MX_SIGNAL_SIGNALED).
// The call fails and the process terminates if the call times out within TU_WATCHDOG_DURATION_NANOSECONDS.

void tu_wait_signaled(mx_handle_t handle);

// Fetch the return code of |process|.

int tu_process_get_return_code(mx_handle_t process);

// Wait for |process| to exit and then fetch its return code.

int tu_process_wait_exit(mx_handle_t process);

// Create an io port.

mx_handle_t tu_io_port_create(uint32_t options);

// Set the system exception port.

void tu_set_system_exception_port(mx_handle_t eport, uint64_t key);

// Set the exception port for |handle| which is a process or thread.

void tu_set_exception_port(mx_handle_t handle, mx_handle_t eport, uint64_t key, uint32_t options);

// Get basic handle info for |handle|.

void tu_handle_get_basic_info(mx_handle_t handle, mx_info_handle_basic_t* info);

__END_CDECLS
