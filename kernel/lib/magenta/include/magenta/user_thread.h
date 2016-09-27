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
#include <magenta/exception.h>
#include <magenta/excp_port.h>
#include <magenta/futex_node.h>
#include <magenta/state_tracker.h>

#include <mxtl/intrusive_double_list.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/string_piece.h>

class ProcessDispatcher;
class ThreadDispatcher;

class UserThread : public mxtl::DoublyLinkedListable<UserThread*>
                 , public mxtl::RefCounted<UserThread> {
public:
    // state of the thread
    enum class State {
        INITIAL,     // newly created thread
        INITIALIZED, // LK thread state is initialized
        RUNNING,     // thread is running
        DYING,       // thread has been signaled for kill, but has not exited yet
        DEAD,        // thread has exited and is not running
    };

    UserThread(mxtl::RefPtr<ProcessDispatcher> process,
               uint32_t flags);
    ~UserThread();

    static UserThread* GetCurrent() {
        return reinterpret_cast<UserThread*>(get_current_thread()->user_thread);
    }

    // Performs initialization on a newly constructed UserThread
    // If this fails, then the object is invalid and should be deleted
    status_t Initialize(mxtl::StringPiece name);
    status_t Start(uintptr_t pc, uintptr_t sp, uintptr_t arg1, uintptr_t arg2);
    void Exit() __NO_RETURN;
    void Kill();
    void DispatcherClosed();

    // accessors
    ProcessDispatcher* process() { return process_.get(); }
    // N.B. The dispatcher() accessor is potentially racy.
    // See UserThread::DispatcherClosed.
    ThreadDispatcher* dispatcher() { return dispatcher_; }

    FutexNode* futex_node() { return &futex_node_; }
    StateTracker* state_tracker() { return &state_tracker_; }
    const mxtl::StringPiece name() const { return thread_.name; }
    State state() const { return state_; }

    status_t SetExceptionPort(ThreadDispatcher* td, mxtl::RefPtr<ExceptionPort> eport);
    void ResetExceptionPort();
    mxtl::RefPtr<ExceptionPort> exception_port();

    // Note this takes a specific exception port as an argument because there are several:
    // debugger, thread, process, and system.
    status_t ExceptionHandlerExchange(mxtl::RefPtr<ExceptionPort> eport,
                                      const mx_exception_report_t* report,
                                      const arch_exception_context_t* arch_context);
    status_t MarkExceptionHandled(mx_exception_status_t status);

    // For debugger usage.
    // TODO(dje): The term "state" here conflicts with "state tracker".
    uint32_t get_num_state_kinds() const;
    // TODO(dje): Consider passing an Array<uint8_t> here and in WriteState.
    status_t ReadState(uint32_t state_kind, void* buffer, uint32_t* buffer_len);
    // |priv| = true -> allow setting privileged values, otherwise leave them unchanged
    // This is useful for, for example, flags registers that consist of both
    // privileged and unprivileged fields.
    status_t WriteState(uint32_t state_kind, const void* buffer, uint32_t buffer_len, bool priv);

    mx_koid_t get_koid() const { return koid_; }
    void set_dispatcher(ThreadDispatcher* dispatcher);

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

    // The kernel object id. Since ProcessDispatcher maintains a list of
    // UserThreads, and since use of dispatcher_ is racy (see
    // UserThread::DispatcherClosed), we keep a copy of the ThreadDispatcher
    // koid here. This allows ProcessDispatcher::LookupThreadById to return
    // the koid of the Dispatcher.
    // At construction time this is MX_KOID_INVALID. Later when set_dispatcher
    // is called this is updated to be the koid of the dispatcher.
    mx_koid_t koid_;

    // a ref pointer back to the parent process
    mxtl::RefPtr<ProcessDispatcher> process_;
    // a naked pointer back to the containing thread dispatcher.
    ThreadDispatcher* dispatcher_ = nullptr;

    // User thread starting register values.
    uintptr_t user_entry_ = 0;
    uintptr_t user_sp_ = 0;
    uintptr_t user_arg1_ = 0;
    uintptr_t user_arg2_ = 0;

    // our State
    State state_ = State::INITIAL;
    Mutex state_lock_;

    // Node for linked list of threads blocked on a futex
    FutexNode futex_node_;

    NonIrqStateTracker state_tracker_;

    // A thread-level exception port for this thread.
    mxtl::RefPtr<ExceptionPort> exception_port_;
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
