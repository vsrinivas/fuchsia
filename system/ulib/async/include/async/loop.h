// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Provides an implementation of a simple thread-safe asynchronous
// dispatcher based on a Zircon completion port.  The implementation
// is designed to avoid most dynamic memory allocation except for that
// which is required to create the loop in the first place or to manage
// the list of running threads.
//
// See README.md for example usage.
//

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <threads.h>

#include <zircon/compiler.h>

#include <async/default.h>
#include <async/dispatcher.h>

__BEGIN_CDECLS

// Message loop configuration structure.
typedef void(async_loop_callback_t)(async_t* async, void* data);
typedef struct async_loop_config {
    // If true, the loop will automatically register itself as the default
    // dispatcher for the thread upon which it was created and will
    // automatically unregister itself when destroyed (which must occur on
    // the same thread).
    //
    // If false, the loop will not do this.  The loop's creator is then
    // resposible for passing around the |async_t| explicitly or calling
    // |async_set_default()| on whatever threads need it.
    //
    // Note that the loop can be used even without setting it as the default.
    bool make_default_for_current_thread;

    // A function to call before the dispatcher invokes each handler, or NULL if none.
    async_loop_callback_t* prologue;

    // A function to call after the dispatcher invokes each handler, or NULL if none.
    async_loop_callback_t* epilogue;

    // Data to pass to the callback functions.
    void* data;
} async_loop_config_t;

// Creates a message loop and returns its asynchronous dispatcher.
// All operations on the message loop are thread-safe (except destroy).
//
// |config| provides configuration for the message loop, may be NULL for
// default behavior.
//
// Returns |ZX_OK| on success.
// Returns |ZX_ERR_NO_MEMORY| if allocation failed.
// May return other errors if the necessary internal handles could not be created.
zx_status_t async_loop_create(const async_loop_config_t* config,
                              async_t** out_async);

// Shuts down the message loop, notifies handlers which asked to handle shutdown.
// The message loop must not currently be running on any threads other than
// those started by |async_loop_start_thread()| which this function will join.
//
// Does nothing if already shutting down.
void async_loop_shutdown(async_t* async);

// Destroys the message loop.
// Implicitly calls |async_loop_shutdown()| and joins all threads started
// using |async_loop_start_thread()| before destroying the loop itself.
void async_loop_destroy(async_t* async);

// Runs the message loop on the current thread.
// This function can be called on multiple threads to setup a multi-threaded
// dispatcher.
//
// Dispatches events until the |deadline| expires or the loop is quitted.
// Use |ZX_TIME_INFINITE| to dispatch events indefinitely.
//
// If |once| is true, performs a single unit of work then returns.
//
// Returns |ZX_OK| if the dispatcher returns after one cycle.
// Returns |ZX_ERR_TIMED_OUT| if the deadline expired.
// Returns |ZX_ERR_CANCELED| if the loop quitted.
// Returns |ZX_ERR_BAD_STATE| if the loop was shut down with |async_loop_shutdown()|.
zx_status_t async_loop_run(async_t* async, zx_time_t deadline, bool once);

// Dispatches events until there are none remaining, and then returns without
// waiting. This is useful for unit testing, because the behavior doesn't depend
// on time.
//
// Returns |ZX_OK| if the dispatcher reaches an idle state.
// Returns |ZX_ERR_CANCELED| if the loop quitted.
// Returns |ZX_ERR_BAD_STATE| if the loop was shut down with |async_loop_shutdown()|.
zx_status_t async_loop_run_until_idle(async_t* async);

// Quits the message loop.
// Active invocations of |async_loop_run()| and threads started using
// |async_loop_start_thread()| will eventually terminate upon completion of their
// current unit of work.
//
// Subsequent calls to |async_loop_run()| or |async_loop_start_thread()|
// will return immediately until |async_loop_reset_quit()| is called.
void async_loop_quit(async_t* async);

// Resets the quit state of the message loop so that it can be restarted
// using |async_loop_run()| or |async_loop_start_thread()|.
//
// This function must only be called when the message loop is not running.
// The caller must ensure all active invocations of |async_loop_run()| and
// threads started using |async_loop_start_thread()| have terminated before
// resetting the quit state.
//
// Returns |ZX_OK| if the loop's state was |ASYNC_LOOP_RUNNABLE| or |ASYNC_LOOP_QUIT|.
// Returns |ZX_ERR_BAD_STATE| if the loop's state was |ASYNC_LOOP_SHUTDOWN| or if
// the message loop is currently active on one or more threads.
zx_status_t async_loop_reset_quit(async_t* async);

// Returns the current state of the message loop.
typedef enum {
    ASYNC_LOOP_RUNNABLE = 0,
    ASYNC_LOOP_QUIT = 1,
    ASYNC_LOOP_SHUTDOWN = 2,
} async_loop_state_t;
async_loop_state_t async_loop_get_state(async_t* async);

// Starts a message loop running on a new thread.
// The thread will run until the loop quits.
//
// |name| is the desired name for the new thread, may be NULL.
// If |out_thread| is not NULL, it is set to the new thread identifier.
//
// Returns |ZX_OK| on success.
// Returns |ZX_ERR_BAD_STATE| if the loop was shut down with |async_loop_shutdown()|.
// Returns |ZX_ERR_NO_MEMORY| if allocation or thread creation failed.
zx_status_t async_loop_start_thread(async_t* async, const char* name,
                                    thrd_t* out_thread);

// Blocks until all dispatch threads started with |async_loop_start_thread()|
// have terminated.
void async_loop_join_threads(async_t* async);

__END_CDECLS
