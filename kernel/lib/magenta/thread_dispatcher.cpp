// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/thread_dispatcher.h>

#include <magenta/handle.h>
#include <trace.h>

#define LOCAL_TRACE 0

constexpr mx_rights_t kDefaultThreadRights =
    MX_RIGHT_READ | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER;

// static
status_t ThreadDispatcher::Create(UserThread* thread, utils::RefPtr<Dispatcher>* dispatcher,
                                  mx_rights_t* rights) {
    Dispatcher* disp = new ThreadDispatcher(thread);
    if (!disp) return ERR_NO_MEMORY;

    *rights = kDefaultThreadRights;
    *dispatcher = utils::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

ThreadDispatcher::ThreadDispatcher(UserThread* thread) : thread_(thread) {
    LTRACE_ENTRY;
}

ThreadDispatcher::~ThreadDispatcher() {
    LTRACE_ENTRY;

    // thread is effectively detached when there are no more handles referring to it
    thread_->Detach();
}

Waiter* ThreadDispatcher::BeginWait(event_t* event, Handle* handle, mx_signals_t signals) {
    return thread_->GetWaiter()->BeginWait(event, handle, signals);
}

void ThreadDispatcher::Close(Handle* handle) {}

status_t ThreadDispatcher::SetExceptionHandler(utils::RefPtr<Dispatcher> handler, mx_exception_behaviour_t behaviour) {
    return thread_->SetExceptionHandler(handler, behaviour);
}

status_t ThreadDispatcher::MarkExceptionHandled(mx_exception_status_t status) {
    // TODO(dje): Verify thread is waiting.
    thread_->WakeFromExceptionHandler(status);
    return NO_ERROR;
}
