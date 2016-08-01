// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/cond.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <kernel/wait.h>
#include <lib/dpc.h>

#include <magenta/dispatcher.h>
#include <magenta/excp_port.h>
#include <magenta/futex_node.h>
#include <magenta/state_tracker.h>

#include <utils/intrusive_double_list.h>
#include <utils/ref_counted.h>
#include <utils/ref_ptr.h>
#include <utils/string_piece.h>

class ProcessDispatcher;

class UserThread : public utils::DoublyLinkedListable<UserThread*>
                 , public utils::RefCounted<UserThread> {
public:
    // state of the thread
    enum class State {
        INITIAL,     // newly created thread
        INITIALIZED, // LK thread state is initialized
        RUNNING,     // thread is running
        DYING,       // thread has been signalled for kill, but has not exited yet
        DEAD,        // thread has exited and is not running
    };

    UserThread(mx_koid_t koid,
               utils::RefPtr<ProcessDispatcher> process,
               uint32_t flags);
    ~UserThread();

    static UserThread* GetCurrent() {
        return reinterpret_cast<UserThread*>(tls_get(TLS_ENTRY_LKUSER));
    }

    // Performs initialization on a newly constructed UserThread
    // If this fails, then the object is invalid and should be deleted
    status_t Initialize(utils::StringPiece name);
    status_t Start(uintptr_t entry, uintptr_t stack, uintptr_t arg);
    void Exit() __NO_RETURN;
    void Kill();
    void DispatcherClosed();

    // accessors
    ProcessDispatcher* process() { return process_.get(); }
    FutexNode* futex_node() { return &futex_node_; }
    StateTracker* state_tracker() { return &state_tracker_; }
    const utils::StringPiece name() const { return thread_.name; }
    State state() const { return state_; }

    status_t SetExceptionPort(ThreadDispatcher* td, utils::RefPtr<ExceptionPort> eport);
    void ResetExceptionPort();
    utils::RefPtr<ExceptionPort> exception_port();

    // Note this takes a specific exception port as an argument because there are several:
    // debugger, thread, process, and system.
    status_t ExceptionHandlerExchange(utils::RefPtr<ExceptionPort> eport, const mx_exception_report_t* report);
    status_t MarkExceptionHandled(mx_exception_status_t status);

    mx_koid_t get_koid() const { return koid_; }

private:
    UserThread(const UserThread&) = delete;
    UserThread& operator=(const UserThread&) = delete;

    // kernel level entry point
    static int StartRoutine(void* arg);

    // callback from kernel when thread is exiting, just before it stops for good.
    void Exiting();
    static void ThreadExitCallback(void* arg);

    // change states of the object, do what is appropriate for the state transition
    void SetState(State);

    // The kernel object id. Since UserThread is not a dispatcher, is an 'inner' koid.
    const mx_koid_t koid_;

    // a ref pointer back to the parent process
    utils::RefPtr<ProcessDispatcher> process_;

    // user thread start routine and argument
    uintptr_t user_entry_ = 0;
    uintptr_t user_arg_ = 0;
    uintptr_t user_stack_ = 0;

    // our State
    State state_ = State::INITIAL;
    Mutex state_lock_;

    // Node for linked list of threads blocked on a futex
    FutexNode futex_node_;

    StateTracker state_tracker_;

    // A thread-level exception port for this thread.
    utils::RefPtr<ExceptionPort> exception_port_;
    Mutex exception_lock_;

    // Support for sending an exception to an exception handler and then waiting for a response.
    mx_exception_status_t exception_status_ = MX_EXCEPTION_STATUS_NOT_HANDLED;
    cond_t exception_wait_cond_ = COND_INITIAL_VALUE(exception_wait_cond_);
    Mutex exception_wait_lock_;

    // cleanup dpc structure
    dpc_t cleanup_dpc_ = {};

    // LK thread structure
    // put last to ease debugging since this is a pretty large structure
    // TODO(dje): Revisit eventually: How large? And how frequently are members
    // of thread_ accessed? [better icache usage if insns can be smaller
    // from smaller offsets]
    thread_t thread_ = {};
};

const char* StateToString(UserThread::State state);
