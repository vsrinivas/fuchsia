// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Interfaces declared in this file are intended for the use of sanitizer
// runtime library implementation code.  Each sanitizer runtime works only
// with the appropriately sanitized build of libc.  These functions should
// never be called when using the unsanitized libc.  But these names are
// always exported so that the libc ABI is uniform across sanitized and
// unsanitized builds (only unsanitized shared library binaries are used at
// link time, including linking the sanitizer runtime shared libraries).

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <threads.h>

__BEGIN_CDECLS

// These are aliases for the functions defined in libc, which are always
// the unsanitized versions.  The sanitizer runtimes can call them by these
// aliases when they are overriding libc's definitions of the unadorned
// symbols.
__typeof(memcpy) __unsanitized_memcpy;
__typeof(memmove) __unsanitized_memmove;
__typeof(memset) __unsanitized_memset;

// The sanitized libc allocates the shadow memory in the appropriate ratio for
// the particular sanitizer (shadow_base == shadow_limit >> SHADOW_SCALE)
// early during startup, before any other address space allocations can occur.
// Shadow memory always starts at address zero:
//     [memory_limit,   UINTPTR_MAX)    Address space reserved by the system.
//     [shadow_limit,   memory_limit)   Address space available to the user.
//     [shadow_base,    shadow_limit)   Shadow memory, preallocated.
//     [0,              shadow_base)    Shadow gap, cannot be allocated.
typedef struct {
    uintptr_t shadow_base;
    uintptr_t shadow_limit;
    uintptr_t memory_limit;
} sanitizer_shadow_bounds_t;
sanitizer_shadow_bounds_t __sanitizer_shadow_bounds(void);

// Write logging information from the sanitizer runtime.  The buffer
// is expected to be printable text with '\n' ending each line.
// Timestamps and globally unique identifiers of the calling process
// and thread (mx_koid_t) are attached to all messages, so there is no
// need to include those details in the text.  The log of messages
// written with this call automatically includes address and ELF build
// ID details of the program and all shared libraries sufficient to
// translate raw address values into program symbols or source
// locations via a post-processor that has access to the original ELF
// files and their debugging information.  The text can contain markup
// around address values that should be resolved symbolically; see
// TODO(mcgrathr) for the format and details of the post-processor.
void __sanitizer_log_write(const char *buffer, size_t len);

// Runtimes that have binary data to publish (e.g. coverage) use this
// interface.  The name describes the data sink that will receive this
// blob of data; the string is not used after this call returns.  The
// caller creates a VMO (e.g. mx_vmo_create) and passes it in; the VMO
// handle is consumed by this call.  Each particular data sink has its
// own conventions about both the format of the data in the VMO and the
// protocol for when data must be written there.  For some sinks, the
// VMO's data is used immediately.  For other sinks, the caller is
// expected to have the VMO mapped in and be writing more data there
// throughout the life of the process, to be analyzed only after the
// process terminates.  Yet others might use an asynchronous shared
// memory protocol between producer and consumer.
void __sanitizer_publish_data(const char* sink_name, mx_handle_t vmo);

// Runtimes that want to read configuration files use this interface.
// The name is a string from the user (something akin to a file name
// but not necessarily actually a file name); the string is not used
// after this call returns.  On success, this yields a read-only VMO
// handle from which the contents associated with that name can be
// read; the caller is responsible for closing this handle.
mx_status_t __sanitizer_get_configuration(const char* config_name,
                                          mx_handle_t* out_vmo);

// The "hook" interfaces are functions that the sanitizer runtime library
// can define and libc will call.  There are default definitions in libc
// which do nothing, but any other definitions will override those.  These
// declarations use __EXPORT (i.e. explicit STV_DEFAULT) to ensure any user
// definitions are seen by libc even if the user code is being compiled
// with -fvisibility=hidden or equivalent.

// This is called at program startup, with the arguments that will be
// passed to main.  This is called before any other application code,
// including both static constructors and initialization of things like
// mxio and mx_get_startup_handle.  It's basically the first thing called
// after libc's most basic internal global initialization is complete and
// the initial thread has switched to its real thread stack.  Since not
// even all of libc's own constructors have run yet, this should not call
// into libc or other library code.
__EXPORT void __sanitizer_startup_hook(int argc, char** argv, char** envp,
                                       void* stack_base, size_t stack_size);

// This is called when a new thread has been created but is not yet
// running.  Its C11 thrd_t value has been determined and its stack has
// been allocated.  All that remains is to actually start the thread
// running (which can fail only in catastrophic bug situations).  Its
// return value will be passed to __sanitizer_thread_create_hook, below.
__EXPORT void *__sanitizer_before_thread_create_hook(
    thrd_t thread, bool detached, const char* name,
    void* stack_base, size_t stack_size);

// This is called after a new thread has been created or creation has
// failed at the final stage; __sanitizer_before_thread_create_hook has
// been called first, and its return value is the first argument here.
// The second argument is what the return value of C11 thrd_create would
// be for this creation attempt (which might have been instigated by
// either thrd_create or pthread_create).  If it's thrd_success, then
// the new thread has now started running.  Otherwise (it's a different
// <threads.h> thrd_* value), thread creation has failed and the thread
// details reported to __sanitizer_before_thread_create_hook will be
// freed without the thread ever starting.
__EXPORT void __sanitizer_thread_create_hook(
    void* hook, thrd_t thread, int error);

// This is called in each new thread as it starts up.  The argument is
// the same one returned by __sanitizer_before_thread_create_hook and
// previously passed to __sanitizer_thread_create_hook.
__EXPORT void __sanitizer_thread_start_hook(void* hook, thrd_t self);

// This is called in each thread just before it dies.
// All thread-specific destructors have been run.
// The argument is the same one passed to __sanitizer_thread_start_hook.
__EXPORT void __sanitizer_thread_exit_hook(void* hook, thrd_t self);

__END_CDECLS
