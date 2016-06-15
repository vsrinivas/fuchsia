// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/thread.h>
#include <kernel/wait.h>
#include <list.h>
#include <magenta/dispatcher.h>
#include <magenta/waiter.h>
#include <magenta/futex_node.h>
#include <utils/ref_ptr.h>
#include <utils/string_piece.h>

class ThreadHandle;
class UserProcess;

class UserThread {
public:
    UserThread(UserProcess* process, thread_start_routine entry, void* arg);
    ~UserThread();

    // Performs initialization on a newly constructed UserThread
    // If this fails, then the object is invalid and should be deleted
    status_t Initialize(utils::StringPiece name);
    void Start();
    void Exit();
    void Detach();

    UserProcess* process() const {
        return process_;
    }

    mx_tid_t id() const {
        return id_;
    }

    static UserThread* GetCurrent() {
        return reinterpret_cast<UserThread*>(tls_get(TLS_ENTRY_LKUSER));
    }

    status_t SetExceptionHandler(utils::RefPtr<Dispatcher> handler, mx_exception_behaviour_t behaviour);
    utils::RefPtr<Dispatcher> exception_handler();

    FutexNode* futex_node() {
        return &futex_node_;
    }

    const utils::StringPiece name() const { return thread_.name; }

    Waiter* GetWaiter() { return &waiter_; }

    // TODO(dje): Support unwinding from this exception and introducing a
    // different exception.
    status_t WaitForExceptionHandler(utils::RefPtr<Dispatcher> dispatcher, const mx_exception_report_t* report);
    void WakeFromExceptionHandler(mx_exception_status_t status);

private:
    UserThread(const UserThread&) = delete;
    UserThread& operator=(const UserThread&) = delete;

    static int StartRoutine(void* arg);

    // my process
    UserProcess* process_;

    // So UserProcess can access node_, thread_, joined_ and detached_
    friend class UserProcess;

    // for my process's thread list
    struct list_node node_;

    // A unique thread id within the process.
    mx_tid_t id_;

    // thread start routine and argument pointer
    thread_start_routine entry_;
    void* arg_;

    // True if the thread has been joined
    // Only set by UserProcess, protected by UserProcess::thread_list_lock_
    bool joined_;
    // We are detached if there are no handles remaining that refer to us.
    // In that case, it is safe to delete this UserThread instance when the thread is joined.
    // Only set by UserProcess, protected by UserProcess::thread_list_lock_
    bool detached_;

    thread_t thread_;
    void* user_stack_;

    // Node for linked list of threads blocked on a futex
    FutexNode futex_node_;

    Waiter waiter_;

    // A thread-level exception handler for this thread.
    utils::RefPtr<Dispatcher> exception_handler_;
    mx_exception_behaviour_t exception_behaviour_;
    mutex_t exception_lock_;

    // Support for waiting for an exception handler.
    // Reply from handler.
    mx_exception_status_t exception_status_;
    cond_t exception_wait_cond_;
    mutex_t exception_wait_lock_;

    static const int kDefaultStackSize = 16 * PAGE_SIZE;
};
