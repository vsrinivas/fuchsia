// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async-loop/loop.h>
#include <lib/async/default.h>
#include <lib/zx/time.h>
#include <stdbool.h>
#include <stddef.h>
#include <threads.h>
#include <zircon/compiler.h>

namespace async {

// C++ wrapper for an asynchronous dispatch loop.
//
// This class is thread-safe.
class Loop {
public:
    // Creates a message loop.
    // All operations on the message loop are thread-safe (except destroy).
    //
    // Note that it's ok to run the loop on a different thread from the one
    // upon which it was created.
    //
    // |config| provides configuration for the message loop.  If null, the behavior
    // is the same as that of a zero-initialized instance of async_loop_config_t.
    //
    // See also |kAsyncLoopConfigAttachToThread| and |kAsyncLoopConfigNoAttachToThread|.
    explicit Loop(const async_loop_config_t* config);

    Loop(const Loop&) = delete;
    Loop(Loop&&) = delete;
    Loop& operator=(const Loop&) = delete;
    Loop& operator=(Loop&&) = delete;

    // Destroys the message loop.
    // Implicitly calls |Shutdown()|.
    ~Loop();

    // Gets the underlying message loop structure.
    async_loop_t* loop() const { return loop_; }

    // Gets the loop's asynchronous dispatch interface.
    async_dispatcher_t* dispatcher() const { return async_loop_get_dispatcher(loop_); }

    // Shuts down the message loop, notifies handlers which asked to handle shutdown.
    // The message loop must not currently be running on any threads other than
    // those started by |StartThread()| which this function will join.
    //
    // Does nothing if already shutting down.
    void Shutdown();

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
    // Returns |ZX_ERR_BAD_STATE| if the loop was shut down with |Shutdown()|.
    zx_status_t Run(zx::time deadline = zx::time::infinite(), bool once = false);

    // Dispatches events until there are none remaining, and then returns
    // without waiting. This is useful for unit testing, because the behavior
    // doesn't depend on time.
    //
    // Returns |ZX_OK| if the dispatcher reaches an idle state.
    // Returns |ZX_ERR_CANCELED| if the loop quitted.
    // Returns |ZX_ERR_BAD_STATE| if the loop was shut down with |Shutdown()|.
    zx_status_t RunUntilIdle();

    // Quits the message loop.
    // Active invocations of |Run()| and threads started using |StartThread()|
    // will eventually terminate upon completion of their current unit of work.
    //
    // Subsequent calls to |Run()| or |StartThread()| will return immediately
    // until |ResetQuit()| is called.
    void Quit();

    // Resets the quit state of the message loop so that it can be restarted
    // using |Run()| or |StartThread()|.
    //
    // This function must only be called when the message loop is not running.
    // The caller must ensure all active invocations of |Run()| and threads
    // started using |StartThread()| have terminated before resetting the quit state.
    //
    // Returns |ZX_OK| if the loop's state was |ASYNC_LOOP_RUNNABLE| or |ASYNC_LOOP_QUIT|.
    // Returns |ZX_ERR_BAD_STATE| if the loop's state was |ASYNC_LOOP_SHUTDOWN| or if
    // the message loop is currently active on one or more threads.
    zx_status_t ResetQuit();

    // Returns the current state of the message loop.
    async_loop_state_t GetState() const;

    // Starts a message loop running on a new thread.
    // The thread will run until the loop quits.
    //
    // |name| is the desired name for the new thread, may be NULL.
    // If |out_thread| is not NULL, it is set to the new thread identifier.
    //
    // Returns |ZX_OK| on success.
    // Returns |ZX_ERR_BAD_STATE| if the loop was shut down with |async_loop_shutdown()|.
    // Returns |ZX_ERR_NO_MEMORY| if allocation or thread creation failed.
    zx_status_t StartThread(const char* name = nullptr, thrd_t* out_thread = nullptr);

    // Blocks until all dispatch threads started with |StartThread()|
    // have terminated.
    void JoinThreads();

private:
    async_loop_t* loop_;
};

} // namespace async
