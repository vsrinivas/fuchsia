// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/dispatcher.h>
#include <magenta/user_thread.h>
#include <sys/types.h>

class ThreadDispatcher : public Dispatcher {
public:
    static status_t Create(mxtl::RefPtr<UserThread> thread, mxtl::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    ~ThreadDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_THREAD; }
    mx_koid_t get_inner_koid() const final { return thread_->get_koid(); }

    mx_status_t Start(uintptr_t pc, uintptr_t sp, uintptr_t arg1, uintptr_t arg2) { return thread_->Start(pc, sp, arg1, arg2); }
    void Kill() { thread_->Kill(); }

    StateTracker* get_state_tracker() final;

    // TODO(dje): Was private. Needed for exception handling.
    // Could provide delegating accessors, but does this need to stay private?
    UserThread* thread() { return thread_.get(); }

    // exception handling support
    status_t SetExceptionPort(mxtl::RefPtr<ExceptionPort> eport);
    void ResetExceptionPort();

private:
    explicit ThreadDispatcher(mxtl::RefPtr<UserThread> thread);

    mxtl::RefPtr<UserThread> thread_;
};
