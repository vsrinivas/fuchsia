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
#include <zircon/types.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/object.h>
#include <launchpad/launchpad.h>

__BEGIN_CDECLS

// Sleep interval in the watchdog thread. Make this short so we don't need to
// wait too long when tearing down in the success case.  This is especially
// helpful when running "while /boot/test/sys/debugger-test; do true; done".
#define TU_WATCHDOG_TICK_DURATION ((int64_t)ZX_MSEC(50))  // 0.05 seconds

// Number of sleep intervals until the watchdog fires.
// Note: There is a tension here between not wanting to block a complete test
// run because of a hung test for too long, vs not wanting to introduce
// flakyness into a test run because of a loaded machine (not uncommon on
// bots). One solution would be a runtime determination of what a good value
// is.
#define TU_WATCHDOG_TIMEOUT_TICKS 100  // 5 seconds

// These malloc-using calls will terminate the process on ENOMEM.
void* tu_malloc(size_t size);
void* tu_calloc(size_t nmemb, size_t size);
char* tu_strdup(const char* s);
char* tu_asprintf(const char* fmt, ...);

// Print a message saying a syscall (or similar) function failed,
// and terminate the process.
// |what| is typically the name of the function that had the syscall failure,
// but it can include more descriptive text as desired.

void tu_fatal(const char *what, zx_status_t status);

// A wrapper on zx_handle_close.

void tu_handle_close(zx_handle_t handle);

// A wrapper on zx_handle_duplicate.

zx_handle_t tu_handle_duplicate(zx_handle_t handle);

// A wrapper on launchpad_launch.

zx_handle_t tu_launch(zx_handle_t job, const char* name,
                      int argc, const char* const* argv,
                      const char* const* envp,
                      size_t num_handles, zx_handle_t* handles,
                      uint32_t* handle_ids);

// The first part of launchpad_launch_fdio_etc that creates the
// launchpad and initializes the process.

launchpad_t* tu_launch_fdio_init(zx_handle_t job, const char* name,
                                 int argc, const char* const* argv,
                                 const char* const* envp,
                                 size_t num_handles, zx_handle_t* handles,
                                 uint32_t* handle_ids);

// The second part of launchpad_launch_fdio_etc that starts the process.
// Returns a handle of the started process.

zx_handle_t tu_launch_fdio_fini(launchpad_t* lp);

// A wrapper on C11 thrd_create.

void tu_thread_create_c11(thrd_t* thread, thrd_start_t entry, void* arg,
                          const char* name);

// A wrapper on zx_object_wait_many that can be easier to call.
// |num_objects| is the number of elements in |handles,signals,pending|.
// Also, this applies |timeout_scale| to |timeout|.

zx_status_t tu_wait(uint32_t num_objects,
                    const zx_handle_t* handles,
                    const zx_signals_t* signals,
                    zx_signals_t* pending,
                    zx_time_t timeout);

// A wrapper on zx_channel_create.

void tu_channel_create(zx_handle_t* handle0, zx_handle_t* handle1);


// A wrapper on zx_channel_write.

void tu_channel_write(zx_handle_t handle, uint32_t flags, const void* bytes, uint32_t num_bytes,
                      const zx_handle_t* handles, uint32_t num_handles);

// A wrapper on zx_channel_read.

void tu_channel_read(zx_handle_t handle, uint32_t flags, void* bytes, uint32_t* num_bytes,
                     zx_handle_t* handles, uint32_t* num_handles);

// Wait for |channel| to be readable.
// Returns true if the channel is readable, and false if the peer has closed its end.
// Note: This waits "forever", and relies on the watchdog to catch hung tests.
bool tu_channel_wait_readable(zx_handle_t channel);

// Wait for |process| to be signaled (ZX_PROCESS_TERMINATED).
// Note: This waits "forever", and relies on the watchdog to catch hung tests.

void tu_process_wait_signaled(zx_handle_t process);

// Return true if |process| has exited.

bool tu_process_has_exited(zx_handle_t process);

// Fetch the return code of |process|.

int tu_process_get_return_code(zx_handle_t process);

// Wait for |process| to exit and then fetch its return code.

int tu_process_wait_exit(zx_handle_t process);

// Return the handle of thread |tid| in |process|.
// Returns ZX_HANDLE_INVALID if the thread is not found (could have died).

zx_handle_t tu_process_get_thread(zx_handle_t process, zx_koid_t tid);

// Fetch the current threads of |process|.
// |max_threads| is the size of the |threads| buffer.
// Returns the actual number of threads at the point in time when the list
// of threads is obtained. It could be larger than |max_threads|.
// See discussion of ZX_INFO_PROCESS_THREADS in object_get_info.md.

size_t tu_process_get_threads(zx_handle_t process, zx_koid_t* threads, size_t max_threads);

// Create a child job of |job|.

zx_handle_t tu_job_create(zx_handle_t job);

// Create an io port.

zx_handle_t tu_io_port_create(void);

// Set the exception port for |handle| which is a process or thread.

void tu_set_exception_port(zx_handle_t handle, zx_handle_t eport, uint64_t key, uint32_t options);

// Add |handle| to the list of things |port| watches.
// When |handle| is signaled with a signal in |signals| a zx_packet_signal_t
// packet is sent to |port| with the key being the koid of |handle|.
void tu_object_wait_async(zx_handle_t handle, zx_handle_t port, zx_signals_t signals);

// Get basic handle info for |handle|.

void tu_handle_get_basic_info(zx_handle_t handle, zx_info_handle_basic_t* info);

// Return the koid of the object of |handle|.

zx_koid_t tu_get_koid(zx_handle_t handle);

// Return the "related" koid of the object of |handle|.

zx_koid_t tu_get_related_koid(zx_handle_t handle);

// Return a handle of thread |tid|.

zx_handle_t tu_get_thread(zx_handle_t proc, zx_koid_t tid);

// Return zx_info_thread_t of |thread|.

zx_info_thread_t tu_thread_get_info(zx_handle_t thread);

// Return true if |thread| is dying or dead.

bool tu_thread_is_dying_or_dead(zx_handle_t thread);

// Run a program and wait for it to exit.
// Any error in trying to run the program is fatal.
// The result is the return code of the child process.

int tu_run_program(const char *progname, int argc, const char** argv);

// A wrapper for /bin/sh -c <command>.

int tu_run_command(const char* progname, const char* cmd);

// Set the scaling factor for timeouts.
// The default is 1. A value of 2 waits twice as long, and so on.
// This is useful when running tests under a debugger or with a
// massive amount of tracing turned on.
// If 0 is passed no change is made.
// Returns the previous value.

int tu_set_timeout_scale(int scale);

// Start the watchdog thread.
// If the watchdog timer expires before it is canceled with
// tu_watchdog_cancel() then the test fails and the process is terminated.

void tu_watchdog_start(void);

// Cancel the watchdog and "join" the watchdog thread.

void tu_watchdog_cancel(void);

__END_CDECLS
