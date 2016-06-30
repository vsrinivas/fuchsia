// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/event.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <kernel/vm/vm_aspace.h>

#include <magenta/dispatcher.h>
#include <magenta/futex_context.h>
#include <magenta/magenta.h>
#include <magenta/types.h>
#include <magenta/user_thread.h>
#include <magenta/waiter.h>

#include <utils/intrusive_double_list.h>
#include <utils/ref_counted.h>
#include <utils/ref_ptr.h>
#include <utils/string_piece.h>

class Dispatcher;

class UserProcess : public utils::RefCounted<UserProcess> {
public:
    // state of the process
    enum class State {
        INITIAL, // initial state, no thread present in process
        RUNNING, // first thread has started and is running
        DYING,   // process has delivered kill signal to all threads
        DEAD,    // all threads have entered DEAD state and potentially dropped refs on process
    };

    UserProcess(utils::StringPiece name);
    ~UserProcess();

    static UserProcess* GetCurrent() {
        UserThread* current = UserThread::GetCurrent();
        DEBUG_ASSERT(current);
        return current->process();
    }

    // Performs initialization on a newly constructed UserProcess
    // If this fails, then the object is invalid and should be deleted
    status_t Initialize();

    // Map a |handle| to an integer which can be given to usermode as a
    // handle value. Uses MapHandleToU32() plus additional mixing.
    mx_handle_t MapHandleToValue(Handle* handle);

    // Maps a handle value into a Handle as long we can verify that
    // it belongs to this process.
    Handle* GetHandle_NoLock(mx_handle_t handle_value);

    // Adds |hadle| to this process handle list. The handle->process_id() is
    // set to this process id().
    void AddHandle(HandleUniquePtr handle);
    void AddHandle_NoLock(HandleUniquePtr handle);

    // Removes the Handle corresponding to |handle_value| from this process
    // handle list.
    HandleUniquePtr RemoveHandle(mx_handle_t handle_value);
    HandleUniquePtr RemoveHandle_NoLock(mx_handle_t handle_value);

    // Puts back the |handle_value| which has not yet been given to another process
    // back into this process.
    void UndoRemoveHandle_NoLock(mx_handle_t handle_value);

    bool GetDispatcher(mx_handle_t handle_value, utils::RefPtr<Dispatcher>* dispatcher,
                       uint32_t* rights);

    // accessors
    mx_pid_t id() const { return id_; }
    mutex_t& handle_table_lock() { return handle_table_lock_; }
    FutexContext* futex_context() { return &futex_context_; }
    Waiter* waiter() { return &waiter_; }
    State state() const { return state_; }
    utils::RefPtr<VmAspace> aspace() { return aspace_; }
    const utils::StringPiece name() const { return name_; }

    char StateChar() const;
    mx_tid_t GetNextThreadId();

    // Starts the process running
    status_t Start(void* arg, mx_vaddr_t vaddr);

    void Exit(int retcode);
    void Kill();
    void DispatcherClosed();

    status_t GetInfo(mx_process_info_t* info);

    // exception handling routines
    status_t SetExceptionHandler(utils::RefPtr<Dispatcher> handler, mx_exception_behaviour_t behaviour);
    utils::RefPtr<Dispatcher> exception_handler();

    // The following two methods can slow and innacurrate and should only be
    // called from diagnostics code.
    uint32_t HandleCount();
    uint32_t ThreadCount();

    // Necessary members for using DoublyLinkedList<UserProcess>.
    UserProcess* list_prev() { return prev_; }
    UserProcess* list_next() { return next_; }
    const UserProcess* list_prev() const { return prev_; }
    const UserProcess* list_next() const { return next_; }
    void list_set_prev(UserProcess* node) { prev_ = node; }
    void list_set_next(UserProcess* node) { next_ = node; }

private:
    UserProcess(const UserProcess&) = delete;
    UserProcess& operator=(const UserProcess&) = delete;

    // Thread lifecycle support
    friend class UserThread;
    status_t AddThread(UserThread* t);
    void RemoveThread(UserThread* t);

    void SetState(State);

    // Kill all threads
    void KillAllThreads();

    mx_pid_t id_ = 0;

    mx_handle_t handle_rand_ = 0;

    // The next thread id to assign.
    // This is an int as we use atomic_add. TODO(dje): wip
    int next_thread_id_ = 1;

    // protects thread_list_, as well as the UserThread joined_ and detached_ flags
    mutex_t thread_list_lock_ = MUTEX_INITIAL_VALUE(thread_list_lock_);

    // list of threads in this process
    utils::DoublyLinkedList<UserThread> thread_list_;

    // a ref to the main thread
    utils::RefPtr<UserThread> main_thread_;

    // our address space
    utils::RefPtr<VmAspace> aspace_;

    // our list of handles
    mutex_t handle_table_lock_ = MUTEX_INITIAL_VALUE(handle_table_lock_); // protects |handles_|.
    utils::DoublyLinkedList<Handle> handles_;

    Waiter waiter_;

    FutexContext futex_context_;

    // our state
    State state_ = State::INITIAL;
    mutex_t state_lock_ = MUTEX_INITIAL_VALUE(state_lock_);

    // process return code
    int retcode_ = 0;

    // main entry point to the process
    thread_start_routine entry_ = nullptr;

    utils::RefPtr<Dispatcher> exception_handler_;
    mx_exception_behaviour_t exception_behaviour_ = MX_EXCEPTION_BEHAVIOUR_DEFAULT;
    mutex_t exception_lock_ = MUTEX_INITIAL_VALUE(exception_lock_);

    // Holds the linked list of processes that magenta.cpp mantains.
    UserProcess* prev_ = nullptr;
    UserProcess* next_ = nullptr;

    // The user-friendly process name. For debug purposes only.
    char name_[THREAD_NAME_LENGTH / 2] = {};
};

const char* StateToString(UserProcess::State state);
