// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/dispatcher.h>
#include <magenta/syscalls/exception.h>
#include <magenta/user_thread.h>
#include <mxtl/canary.h>
#include <sys/types.h>

class ThreadDispatcher : public Dispatcher {
public:
    static status_t Create(mxtl::RefPtr<UserThread> thread, mxtl::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    ~ThreadDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_THREAD; }
    mx_koid_t get_related_koid() const final;

    mx_status_t Start(uintptr_t pc, uintptr_t sp,
                      uintptr_t arg1, uintptr_t arg2, bool initial_thread) {
        return thread_->Start(pc, sp, arg1, arg2, initial_thread);
    }
    void Kill() { thread_->Kill(); }

    status_t GetInfo(mx_info_thread_t* info);
    status_t GetStats(mx_info_thread_stats_t* info);

    status_t GetExceptionReport(mx_exception_report_t* report);

    mx_status_t Resume() { return thread_->Resume(); }
    mx_status_t Suspend() { return thread_->Suspend(); }

    StateTracker* get_state_tracker() final;

    // TODO(dje): Was private. Needed for exception handling.
    // Could provide delegating accessors, but does this need to stay private?
    UserThread* thread() { return thread_.get(); }

    // exception handling support
    status_t SetExceptionPort(mxtl::RefPtr<ExceptionPort> eport);
    // Returns true if a port had been set.
    bool ResetExceptionPort(bool quietly);

    void get_name(char out_name[MX_MAX_NAME_LEN]) const final;
    status_t set_name(const char* name, size_t len) final;

private:
    explicit ThreadDispatcher(mxtl::RefPtr<UserThread> thread);

    mxtl::Canary<mxtl::magic("THRD")> canary_;
    mxtl::RefPtr<UserThread> thread_;
};
