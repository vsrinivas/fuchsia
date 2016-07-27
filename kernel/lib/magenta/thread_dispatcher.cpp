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
    MX_RIGHT_READ | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER;

// static
status_t ThreadDispatcher::Create(utils::RefPtr<UserThread> thread, utils::RefPtr<Dispatcher>* dispatcher,
                                  mx_rights_t* rights) {
    AllocChecker ac;
    Dispatcher* disp = new (&ac) ThreadDispatcher(thread);
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultThreadRights;
    *dispatcher = utils::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

ThreadDispatcher::ThreadDispatcher(utils::RefPtr<UserThread> thread)
    : thread_(utils::move(thread)) {
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

status_t ThreadDispatcher::SetExceptionHandler(utils::RefPtr<Dispatcher> handler, mx_exception_behaviour_t behaviour) {
    return thread_->SetExceptionHandler(handler, behaviour);
}

status_t ThreadDispatcher::MarkExceptionHandled(mx_exception_status_t status) {
    // TODO(dje): Verify thread is waiting.
    thread_->WakeFromExceptionHandler(status);
    return NO_ERROR;
}
