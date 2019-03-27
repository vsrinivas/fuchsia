// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/suspend_token_dispatcher.h>

#include <err.h>

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <lib/counters.h>
#include <kernel/auto_lock.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <zircon/rights.h>

KCOUNTER(dispatcher_suspend_token_create_count, "dispatcher.suspend_token.create")
KCOUNTER(dispatcher_suspend_token_destroy_count, "dispatcher.suspend_token.destroy")

namespace {

// Suspends a process or thread.
// TODO(ZX-858): Add support for jobs.
zx_status_t SuspendTask(fbl::RefPtr<Dispatcher> task) {
    if (auto thread = DownCastDispatcher<ThreadDispatcher>(&task)) {
        if (thread.get() == ThreadDispatcher::GetCurrent())
            return ZX_ERR_NOT_SUPPORTED;
        return thread->Suspend();
    }

    if (auto process = DownCastDispatcher<ProcessDispatcher>(&task)) {
        if (process.get() == ProcessDispatcher::GetCurrent())
            return ZX_ERR_NOT_SUPPORTED;
        return process->Suspend();
    }

    return ZX_ERR_WRONG_TYPE;
}

// Resumes a process or thread.
// TODO(ZX-858): Add support for jobs.
void ResumeTask(fbl::RefPtr<Dispatcher> task) {
    if (auto thread = DownCastDispatcher<ThreadDispatcher>(&task)) {
        thread->Resume();
        return;
    }

    if (auto process = DownCastDispatcher<ProcessDispatcher>(&task)) {
        process->Resume();
        return;
    }

    __UNREACHABLE;
}

} // namespace

zx_status_t SuspendTokenDispatcher::Create(fbl::RefPtr<Dispatcher> task,
                                           fbl::RefPtr<SuspendTokenDispatcher>* dispatcher,
                                           zx_rights_t* rights) {
    fbl::AllocChecker ac;
    ktl::unique_ptr<SuspendTokenDispatcher> disp(new (&ac) SuspendTokenDispatcher(task));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    zx_status_t status = SuspendTask(ktl::move(task));
    if (status != ZX_OK)
        return status;

    *rights = default_rights();
    *dispatcher = fbl::AdoptRef(disp.release());
    return ZX_OK;
}

SuspendTokenDispatcher::SuspendTokenDispatcher(fbl::RefPtr<Dispatcher> task)
    : task_(ktl::move(task)) {
    kcounter_add(dispatcher_suspend_token_create_count, 1);
}

SuspendTokenDispatcher::~SuspendTokenDispatcher() {
    kcounter_add(dispatcher_suspend_token_destroy_count, 1);
}

void SuspendTokenDispatcher::on_zero_handles() {
    // This is only called once and we're done with |task_| afterwards so we can move it out.
    ResumeTask(ktl::move(task_));
}
