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
#include <utils/ref_ptr.h>
#include <utils/string_piece.h>

class Dispatcher;

class UserProcess {
public:
    // state of the process
    enum State {
        PROC_STATE_INITIAL,
        PROC_STATE_LOADING,
        PROC_STATE_LOADED,
        PROC_STATE_STARTING,
        PROC_STATE_RUNNING,
        PROC_STATE_DEAD,
    };

    UserProcess(utils::StringPiece name);
    ~UserProcess();

    mx_pid_t id() const {
        return id_;
    }

    // Performs initialization on a newly constructed UserProcess
    // If this fails, then the object is invalid and should be deleted
    status_t Initialize();

    // Called when the process owner handle is closed
    void Close();

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

    mutex_t& handle_table_lock() { return handle_table_lock_; }

    static UserProcess* GetCurrent() {
        UserThread *current = UserThread::GetCurrent();
        DEBUG_ASSERT(current);
        return current->process();
    }
    FutexContext* futex_context() { return &futex_context_; }

    Waiter* GetWaiter() { return &waiter_; }

    // Starts the process running
    // The process must first be in state PROC_STATE_LOADED
    status_t Start(void* arg, mx_vaddr_t vaddr);

    void Exit(int retcode);
    void Kill();

    status_t GetInfo(mx_process_info_t *info);

    mx_tid_t GetNextThreadId();

    utils::RefPtr<VmAspace> aspace() { return aspace_; }

    const utils::StringPiece name() const { return name_; }

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
    void AddThread(UserThread* t);
    void ThreadDetached(UserThread* t);

    // Transition to dead state and signal waiter.
    void Dead_NoLock();

    mx_pid_t id_;

    mx_handle_t handle_rand_;

    // The next thread id to assign.
    // This is an int as we use atomic_add. TODO(dje): wip
    int next_thread_id_;

    // protects thread_list_, as well as the UserThread joined_ and detached_ flags
    mutex_t thread_list_lock_;
    // list of running threads
    struct list_node thread_list_;

    // our address space
    utils::RefPtr<VmAspace> aspace_;

    mutex_t handle_table_lock_;  // protects |handles_|.
    utils::DoublyLinkedList<Handle> handles_;

    Waiter waiter_;

    FutexContext futex_context_;

    State state_;
    mutex_t state_lock_;

    // process return code
    int retcode_;

    // main entry point to the process
    thread_start_routine entry_;

    utils::RefPtr<Dispatcher> exception_handler_;
    mx_exception_behaviour_t exception_behaviour_;
    mutex_t exception_lock_;

    // Holds the linked list of processes that magenta.cpp mantains.
    UserProcess* prev_;
    UserProcess* next_;

    // The user-friendly process name. For debug purposes only.
    char name_[THREAD_NAME_LENGTH / 2];
};
