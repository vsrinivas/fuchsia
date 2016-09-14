// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/thread_dispatcher.h>

#include <new.h>

#include <magenta/handle.h>
#include <trace.h>

#define LOCAL_TRACE 0

constexpr mx_rights_t kDefaultThreadRights =
    MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER |
    MX_RIGHT_GET_PROPERTY;

// static
status_t ThreadDispatcher::Create(mxtl::RefPtr<UserThread> thread, mxtl::RefPtr<Dispatcher>* dispatcher,
                                  mx_rights_t* rights) {
    AllocChecker ac;
    Dispatcher* disp = new (&ac) ThreadDispatcher(thread);
    if (!ac.check())
        return ERR_NO_MEMORY;

    thread->set_dispatcher(disp->get_specific<ThreadDispatcher>());

    *rights = kDefaultThreadRights;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

ThreadDispatcher::ThreadDispatcher(mxtl::RefPtr<UserThread> thread)
    : thread_(mxtl::move(thread)) {
    LTRACE_ENTRY_OBJ;

    LTRACEF("thread %p\n", get_current_thread());
}

ThreadDispatcher::~ThreadDispatcher() {
    LTRACE_ENTRY_OBJ;

    thread_->DispatcherClosed();
}

StateTracker* ThreadDispatcher::get_state_tracker() {
    return thread_->state_tracker();
}

status_t ThreadDispatcher::SetExceptionPort(mxtl::RefPtr<ExceptionPort> eport) {
    return thread_->SetExceptionPort(this, eport);
}

void ThreadDispatcher::ResetExceptionPort() {
    return thread_->ResetExceptionPort();
}
