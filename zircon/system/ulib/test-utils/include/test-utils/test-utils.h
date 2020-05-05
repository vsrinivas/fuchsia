// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TEST_UTILS_TEST_UTILS_H_
#define TEST_UTILS_TEST_UTILS_H_

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
#include <zircon/compiler.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Print a message saying a syscall (or similar) function failed,
// and terminate the process.
// |what| is typically the name of the function that had the syscall failure,
// but it can include more descriptive text as desired.

void tu_fatal(const char* what, zx_status_t status) __NO_RETURN;

// Sets up and starts a new process with the given parameters.

zx_handle_t tu_launch_process(zx_handle_t job, const char* name, int argc, const char* const* argv,
                              int envc, const char* const* envp, size_t num_handles,
                              zx_handle_t* handles, uint32_t* handle_ids);

// Opaque type representing launch state.
// Use of this object is not thread-safe.

typedef struct springboard springboard_t;

// Returns the process handle associated with the springboard object.
// The handle is still owned by the input object, and must not be closed or
// transferred.
// This handle will be valid for the lifetime of the springboard object.

zx_handle_t springboard_get_process_handle(springboard_t* sb);

// Returns the root VMAR handle associated with the springboard object.
// The handle is still owned by the input object, and must not be closed or
// transferred.
// This handle will be valid for the lifetime of the springboard object.

zx_handle_t springboard_get_root_vmar_handle(springboard_t* sb);

// Replace the bootstrap channel to be sent to the new process
// with the given handle.
void springboard_set_bootstrap(springboard_t* sb, zx_handle_t);

// Initializes a process.

springboard_t* tu_launch_init(zx_handle_t job, const char* name, int argc, const char* const* argv,
                              int envc, const char* const* envp, size_t num_handles,
                              zx_handle_t* handles, uint32_t* handle_ids);

// Starts the process.
// Returns a handle of the started process.
// The given springboard object becomes invalid after this function returns.

zx_handle_t tu_launch_fini(springboard_t* sb);

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

// Creates an exception channel for |task| which is a job, process, or thread.

zx_handle_t tu_create_exception_channel(zx_handle_t task, uint32_t options);

// Extracts task handles from an exception.

zx_handle_t tu_exception_get_process(zx_handle_t exception);
zx_handle_t tu_exception_get_thread(zx_handle_t exception);

// A ZX_EXCP_SW_BREAKPOINT requires some registers tune-up in order to be handled correctly
// depending on the architecture. This functions takes care of the correct setup of the program
// counter so that the exception can be resumed successfully.
zx_status_t tu_cleanup_breakpoint(zx_handle_t thread);

void tu_resume_from_exception(zx_handle_t exception_handle);

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

// Return zx_info_thread_t of |thread|.

zx_info_thread_t tu_thread_get_info(zx_handle_t thread);

// Return the state of |thread|, one of ZX_THREAD_STATE_*.

uint32_t tu_thread_get_state(zx_handle_t thread);

const char* tu_exception_to_string(uint32_t exception);

__END_CDECLS

#endif  // TEST_UTILS_TEST_UTILS_H_
