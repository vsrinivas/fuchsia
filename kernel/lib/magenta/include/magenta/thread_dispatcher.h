// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/dispatcher.h>
#include <magenta/user_thread.h>
#include <sys/types.h>

class UserThread;

class ThreadDispatcher : public Dispatcher {
public:
    static status_t Create(utils::RefPtr<UserThread> thread, utils::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    virtual ~ThreadDispatcher() final;
    mx_obj_type_t GetType() const final { return MX_OBJ_TYPE_THREAD; }
    ThreadDispatcher* get_thread_dispatcher() final { return this; }

    Waiter* get_waiter() final;
    status_t SetExceptionHandler(utils::RefPtr<Dispatcher> handler, mx_exception_behaviour_t behaviour);
    status_t MarkExceptionHandled(mx_exception_status_t status);

private:
    explicit ThreadDispatcher(utils::RefPtr<UserThread> thread);

    UserThread* thread() { return thread_.get(); }

    utils::RefPtr<UserThread> thread_;
};
