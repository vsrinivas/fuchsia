// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/thread_dispatcher.h>

#include <new.h>
#include <trace.h>

#include <magenta/handle.h>
#include <magenta/process_dispatcher.h>

#define LOCAL_TRACE 0

constexpr mx_rights_t kDefaultThreadRights =
    MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER |
    MX_RIGHT_GET_PROPERTY | MX_RIGHT_SET_PROPERTY;

// static
status_t ThreadDispatcher::Create(mxtl::RefPtr<UserThread> thread, mxtl::RefPtr<Dispatcher>* dispatcher,
                                  mx_rights_t* rights) {
    AllocChecker ac;
    auto disp = mxtl::AdoptRef(new (&ac) ThreadDispatcher(thread));
    if (!ac.check())
        return ERR_NO_MEMORY;

    thread->set_dispatcher(disp.get());

    *rights = kDefaultThreadRights;
    *dispatcher = mxtl::move(disp);
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

mx_koid_t ThreadDispatcher::get_related_koid() const {
    return thread_->process()->get_koid();
}

status_t ThreadDispatcher::SetExceptionPort(mxtl::RefPtr<ExceptionPort> eport) {
    return thread_->SetExceptionPort(this, eport);
}

void ThreadDispatcher::ResetExceptionPort(bool quietly) {
    return thread_->ResetExceptionPort(quietly);
}

void ThreadDispatcher::get_name(char out_name[MX_MAX_NAME_LEN]) const {
    thread_->get_name(out_name);
}

status_t ThreadDispatcher::set_name(const char* name, size_t len) {
    return thread_->set_name(name, len);
}
