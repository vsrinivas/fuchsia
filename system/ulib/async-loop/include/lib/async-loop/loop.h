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

#ifndef LIB_ASYNC_LOOP_LOOP_H_
#define LIB_ASYNC_LOOP_LOOP_H_

#include <stdbool.h>
#include <stddef.h>
#include <threads.h>

#include <zircon/compiler.h>

#include <lib/async/dispatcher.h>

__BEGIN_CDECLS

// Pointer to a message loop created using |async_loop_create()|.
typedef struct async_loop async_loop_t;

// Message loop configuration structure.
typedef void(async_loop_callback_t)(async_loop_t* loop, void* data);
typedef struct async_loop_config {
    // If true, the loop will automatically register itself as the default
    // dispatcher for the thread upon which it was created and will
    // automatically unregister itself when destroyed (which must occur on
    // the same thread).
    //
    // If false, the loop will not do this.  The loop's creator is then
    // resposible for retrieving the loop's dispatcher using |async_loop_get_dispatcher()|
    // and passing it around explicitly or calling |async_set_default_dispatcher()| as needed.
    //
    // Note that the loop can be used even without setting it as the current
    // thread's default.
    bool make_default_for_current_thread;

    // A function to call before the dispatcher invokes each handler, or NULL if none.
    async_loop_callback_t* prologue;

    // A function to call after the dispatcher invokes each handler, or NULL if none.
    async_loop_callback_t* epilogue;

    // Data to pass to the callback functions.
    void* data;
} async_loop_config_t;

// TODO(davemoore): Remove once all other layers have been migrated to new
// constants
extern const async_loop_config_t kAsyncLoopConfigMakeDefault;

// Simple config that when passed to async_loop_create will create a loop
// that will automatically register itself as the default
// dispatcher for the thread upon which it was created and will
// automatically unregister itself when destroyed (which must occur on
// the same thread).

extern const async_loop_config_t kAsyncLoopConfigAttachToThread;
// Simple config that when passed to async_loop_create will create a loop
// that is not registered to the current thread.
extern const async_loop_config_t kAsyncLoopConfigNoAttachToThread;

// Creates a message loop and returns a pointer to it in |out_loop|.
// All operations on the message loop are thread-safe (except destroy).
//
// Note that it's ok to run the loop on a different thread from the one
// upon which it was created.
//
// |config| provides configuration for the message loop. Must not be null.
//
// Returns |ZX_OK| on success.
// Returns |ZX_ERR_NO_MEMORY| if allocation failed.
// May return other errors if the necessary internal handles could not be created.
//
// See also |kAsyncLoopConfigAttachToThread| and |kAsyncLoopConfigNoAttachToThread|.
zx_status_t async_loop_create(const async_loop_config_t* config,
                              async_loop_t** out_loop);

// Gets the the message loop's asynchronous dispatch interface.
async_dispatcher_t* async_loop_get_dispatcher(async_loop_t* loop);

// Gets the message loop associated with the specified asynchronous dispatch interface
//
// This function assumes the dispatcher is backed by an |async_loop_t| which was created
// using |async_loop_create()|.  Its behavior is undefined if used with other dispatcher
// implementations.
async_loop_t* async_loop_from_dispatcher(async_dispatcher_t* dispatcher);

// Shuts down the message loop, notifies handlers which asked to handle shutdown.
// The message loop must not currently be running on any threads other than
// those started by |async_loop_start_thread()| which this function will join.
//
// Does nothing if already shutting down.
void async_loop_shutdown(async_loop_t* loop);

// Destroys the message loop.
// Implicitly calls |async_loop_shutdown()| and joins all threads started
// using |async_loop_start_thread()| before destroying the loop itself.
void async_loop_destroy(async_loop_t* loop);

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
zx_status_t async_loop_run(async_loop_t* loop, zx_time_t deadline, bool once);

// Dispatches events until there are none remaining, and then returns without
// waiting. This is useful for unit testing, because the behavior doesn't depend
// on time.
//
// Returns |ZX_OK| if the dispatcher reaches an idle state.
// Returns |ZX_ERR_CANCELED| if the loop quitted.
// Returns |ZX_ERR_BAD_STATE| if the loop was shut down with |async_loop_shutdown()|.
zx_status_t async_loop_run_until_idle(async_loop_t* loop);

// Quits the message loop.
// Active invocations of |async_loop_run()| and threads started using
// |async_loop_start_thread()| will eventually terminate upon completion of their
// current unit of work.
//
// Subsequent calls to |async_loop_run()| or |async_loop_start_thread()|
// will return immediately until |async_loop_reset_quit()| is called.
void async_loop_quit(async_loop_t* loop);

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
zx_status_t async_loop_reset_quit(async_loop_t* loop);

// Returns the current state of the message loop.
typedef uint32_t async_loop_state_t;
#define ASYNC_LOOP_RUNNABLE ((async_loop_state_t) 0)
#define ASYNC_LOOP_QUIT ((async_loop_state_t) 1)
#define ASYNC_LOOP_SHUTDOWN ((async_loop_state_t) 2)
async_loop_state_t async_loop_get_state(async_loop_t* loop);

// Starts a message loop running on a new thread.
// The thread will run until the loop quits.
//
// |name| is the desired name for the new thread, may be NULL.
// If |out_thread| is not NULL, it is set to the new thread identifier.
//
// Returns |ZX_OK| on success.
// Returns |ZX_ERR_BAD_STATE| if the loop was shut down with |async_loop_shutdown()|.
// Returns |ZX_ERR_NO_MEMORY| if allocation or thread creation failed.
zx_status_t async_loop_start_thread(async_loop_t* loop, const char* name,
                                    thrd_t* out_thread);

// Blocks until all dispatch threads started with |async_loop_start_thread()|
// have terminated.
void async_loop_join_threads(async_loop_t* loop);

__END_CDECLS

#endif  // LIB_ASYNC_LOOP_LOOP_H_