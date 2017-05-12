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
#include <magenta/syscalls/object.h>
#include <launchpad/launchpad.h>

__BEGIN_CDECLS

// Basic watchdog/timeout duration.
#define TU_WATCHDOG_DURATION_SECONDS 2
#define TU_WATCHDOG_DURATION_NANOSECONDS ((int64_t) TU_WATCHDOG_DURATION_SECONDS * 1000 * 1000 * 1000)

void* tu_malloc(size_t size);

char* tu_strdup(const char* s);

char* tu_asprintf(const char* fmt, ...);

// Print a message saying a syscall (or similar) function failed,
// and terminate the process.
// |what| is typically the name of the function that had the syscall failure,
// but it can include more descriptive text as desired.

void tu_fatal(const char *what, mx_status_t status);

// A wrapper on mx_handle_close.

void tu_handle_close(mx_handle_t handle);

// A wrapper on launchpad_launch.

mx_handle_t tu_launch(mx_handle_t job, const char* name,
                      int argc, const char* const* argv,
                      const char* const* envp,
                      size_t num_handles, mx_handle_t* handles,
                      uint32_t* handle_ids);

// The first part of launchpad_launch_mxio_etc that creates the
// launchpad and initializes the process.

launchpad_t* tu_launch_mxio_init(mx_handle_t job, const char* name,
                                 int argc, const char* const* argv,
                                 const char* const* envp,
                                 size_t num_handles, mx_handle_t* handles,
                                 uint32_t* handle_ids);

// The second part of launchpad_launch_mxio_etc that starts the process.
// Returns a handle of the started process.

mx_handle_t tu_launch_mxio_fini(launchpad_t* lp);

// A wrapper on C11 thrd_create.

void tu_thread_create_c11(thrd_t* thread, thrd_start_t entry, void* arg,
                          const char* name);

// A wrapper on mx_channel_create.

void tu_channel_create(mx_handle_t* handle0, mx_handle_t* handle1);


// A wrapper on mx_channel_write.

void tu_channel_write(mx_handle_t handle, uint32_t flags, const void* bytes, uint32_t num_bytes,
                      const mx_handle_t* handles, uint32_t num_handles);

// A wrapper on mx_channel_read.

void tu_channel_read(mx_handle_t handle, uint32_t flags, void* bytes, uint32_t* num_bytes,
                     mx_handle_t* handles, uint32_t* num_handles);

// Wait for |channel| to be readable.
// Returns true if the channel is readable, and false if the peer has closed its end.
// The call fails and the process terminates if the call times out within TU_WATCHDOG_DURATION_NANOSECONDS.
bool tu_channel_wait_readable(mx_handle_t channel);

// Wait for |process| to be signaled (MX_PROCESS_TERMINATED).
// The call fails and the calling process terminates if the call times out within TU_WATCHDOG_DURATION_NANOSECONDS.

void tu_process_wait_signaled(mx_handle_t process);

// Return true if |process| has exited.

bool tu_process_has_exited(mx_handle_t process);

// Fetch the return code of |process|.

int tu_process_get_return_code(mx_handle_t process);

// Wait for |process| to exit and then fetch its return code.

int tu_process_wait_exit(mx_handle_t process);

// Create a child job of |job|.

mx_handle_t tu_job_create(mx_handle_t job);

// Create an io port.

mx_handle_t tu_io_port_create(void);

// Set the system exception port.

void tu_set_system_exception_port(mx_handle_t eport, uint64_t key);

// Set the exception port for |handle| which is a process or thread.

void tu_set_exception_port(mx_handle_t handle, mx_handle_t eport, uint64_t key, uint32_t options);

// Get basic handle info for |handle|.

void tu_handle_get_basic_info(mx_handle_t handle, mx_info_handle_basic_t* info);

// Return the koid of the object of |handle|.

mx_koid_t tu_get_koid(mx_handle_t handle);

// Return the "related" koid of the object of |handle|.

mx_koid_t tu_get_related_koid(mx_handle_t handle);

// Return a handle of thread |tid|.

mx_handle_t tu_get_thread(mx_handle_t proc, mx_koid_t tid);

// Return mx_info_thread_t of |thread|.

mx_info_thread_t tu_thread_get_info(mx_handle_t thread);

// Run a program and wait for it to exit.
// Any error in trying to run the program is fatal.
// The result is the return code of the child process.

int tu_run_program(const char *progname, int argc, const char** argv);

// A wrapper for /bin/sh -c <command>.

int tu_run_command(const char* progname, const char* cmd);

__END_CDECLS
