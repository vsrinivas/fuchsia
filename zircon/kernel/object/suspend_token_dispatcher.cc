// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/suspend_token_dispatcher.h"

#include <err.h>
#include <lib/counters.h>
#include <zircon/rights.h>

#include <fbl/alloc_checker.h>
#include <kernel/auto_lock.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

KCOUNTER(dispatcher_suspend_token_create_count, "dispatcher.suspend_token.create")
KCOUNTER(dispatcher_suspend_token_destroy_count, "dispatcher.suspend_token.destroy")

namespace {

// Suspends a process or thread.
// TODO(fxbug.dev/30807): Add support for jobs.
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
// TODO(fxbug.dev/30807): Add support for jobs.
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

}  // namespace

zx_status_t SuspendTokenDispatcher::Create(fbl::RefPtr<Dispatcher> task,
                                           KernelHandle<SuspendTokenDispatcher>* handle,
                                           zx_rights_t* rights) {
  fbl::AllocChecker ac;
  KernelHandle new_handle(fbl::AdoptRef(new (&ac) SuspendTokenDispatcher()));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  zx_status_t status = SuspendTask(task);
  if (status != ZX_OK)
    return status;

  // Save the task after suspending so that on_zero_handles() resumes it.
  new_handle.dispatcher()->task_ = ktl::move(task);

  *rights = default_rights();
  *handle = ktl::move(new_handle);
  return ZX_OK;
}

SuspendTokenDispatcher::SuspendTokenDispatcher() {
  kcounter_add(dispatcher_suspend_token_create_count, 1);
}

SuspendTokenDispatcher::~SuspendTokenDispatcher() {
  kcounter_add(dispatcher_suspend_token_destroy_count, 1);
}

void SuspendTokenDispatcher::on_zero_handles() {
  // This is only called once and we're done with |task_| afterwards so we can move it out.
  if (task_)
    ResumeTask(ktl::move(task_));
}
