// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/exception_dispatcher.h"

#include <assert.h>
#include <lib/counters.h>

#include <fbl/alloc_checker.h>
#include <ktl/move.h>
#include <object/process_dispatcher.h>

KCOUNTER(dispatcher_exception_create_count, "dispatcher.exception.create")
KCOUNTER(dispatcher_exception_destroy_count, "dispatcher.exception.destroy")

zx_exception_report_t ExceptionDispatcher::BuildArchReport(
    uint32_t type, const arch_exception_context_t& context) {
  zx_exception_report_t report = {};
  report.header.size = sizeof(report);
  report.header.type = type;
  arch_fill_in_exception_context(&context, &report);
  return report;
}

fbl::RefPtr<ExceptionDispatcher> ExceptionDispatcher::Create(
    fbl::RefPtr<ThreadDispatcher> thread, zx_excp_type_t exception_type,
    const zx_exception_report_t* report, const arch_exception_context_t* arch_context) {
  fbl::AllocChecker ac;
  fbl::RefPtr<ExceptionDispatcher> exception = fbl::AdoptRef(
      new (&ac) ExceptionDispatcher(ktl::move(thread), exception_type, report, arch_context));
  if (!ac.check()) {
    // ExceptionDispatchers are small so if we get to this point a lot of
    // other things will be failing too, but we could potentially pre-
    // allocate space for an ExceptionDispatcher in each thread if we want
    // to eliminate this case.
    return nullptr;
  }

  return exception;
}

ExceptionDispatcher::ExceptionDispatcher(fbl::RefPtr<ThreadDispatcher> thread,
                                         zx_excp_type_t exception_type,
                                         const zx_exception_report_t* report,
                                         const arch_exception_context_t* arch_context)
    : thread_(ktl::move(thread)),
      exception_type_(exception_type),
      report_(report),
      arch_context_(arch_context),
      response_event_(EVENT_FLAG_AUTOUNSIGNAL) {
  kcounter_add(dispatcher_exception_create_count, 1);
}

ExceptionDispatcher::~ExceptionDispatcher() { kcounter_add(dispatcher_exception_destroy_count, 1); }

bool ExceptionDispatcher::FillReport(zx_exception_report_t* report) const {
  canary_.Assert();

  Guard<fbl::Mutex> guard{get_lock()};
  if (report_) {
    *report = *report_;
    return true;
  }
  return false;
}

void ExceptionDispatcher::SetTaskRights(zx_rights_t thread_rights, zx_rights_t process_rights) {
  canary_.Assert();

  Guard<fbl::Mutex> guard{get_lock()};
  thread_rights_ = thread_rights;
  process_rights_ = process_rights;
}

zx_status_t ExceptionDispatcher::MakeThreadHandle(HandleOwner* handle) const {
  canary_.Assert();

  Guard<fbl::Mutex> guard{get_lock()};

  if (thread_rights_ == 0) {
    return ZX_ERR_ACCESS_DENIED;
  }

  *handle = Handle::Make(thread_, thread_rights_);
  if (!(*handle)) {
    return ZX_ERR_NO_MEMORY;
  }
  return ZX_OK;
}

zx_status_t ExceptionDispatcher::MakeProcessHandle(HandleOwner* handle) const {
  canary_.Assert();

  Guard<fbl::Mutex> guard{get_lock()};

  if (process_rights_ == 0) {
    return ZX_ERR_ACCESS_DENIED;
  }

  // We have a RefPtr to |thread_| so it can't die, and the thread keeps its
  // process alive, so we know the process is safe to wrap in a RefPtr.
  *handle = Handle::Make(fbl::RefPtr(thread_->process()), process_rights_);
  if (!(*handle)) {
    return ZX_ERR_NO_MEMORY;
  }
  return ZX_OK;
}

void ExceptionDispatcher::on_zero_handles() {
  canary_.Assert();

  response_event_.Signal();
}

void ExceptionDispatcher::GetResumeThreadOnClose(bool* resume_on_close) const {
  canary_.Assert();

  Guard<fbl::Mutex> guard{get_lock()};
  *resume_on_close = resume_on_close_;
}

void ExceptionDispatcher::SetResumeThreadOnClose(bool resume_on_close) {
  canary_.Assert();

  Guard<fbl::Mutex> guard{get_lock()};
  resume_on_close_ = resume_on_close;
}

zx_status_t ExceptionDispatcher::WaitForHandleClose() {
  canary_.Assert();

  zx_status_t status;
  do {
    // Continue to wait for the exception response if we get suspended.
    // Both the suspension and the exception need to be closed out before
    // the thread can resume.
    status = response_event_.WaitWithMask(THREAD_SIGNAL_SUSPEND);
  } while (status == ZX_ERR_INTERNAL_INTR_RETRY);

  if (status == ZX_ERR_INTERNAL_INTR_KILLED) {
    // If the thread was killed it doesn't matter whether the handler
    // wanted to resume or not.
    return ZX_ERR_INTERNAL_INTR_KILLED;
  } else if (status != ZX_OK) {
    // Our event wait should only ever return one of the internal errors
    // handled above or the ZX_OK we send in on_zero_handles().
    ASSERT_MSG(false, "unexpected exception event result: %d\n", status);
    __UNREACHABLE;
  }

  // Return the close action and reset it for next time.
  Guard<fbl::Mutex> guard{get_lock()};
  status = resume_on_close_ ? ZX_OK : ZX_ERR_NEXT;
  resume_on_close_ = false;
  return status;
}

void ExceptionDispatcher::DiscardHandleClose() {
  canary_.Assert();

  response_event_.Unsignal();

  Guard<fbl::Mutex> guard{get_lock()};
  resume_on_close_ = false;
}

void ExceptionDispatcher::Clear() {
  canary_.Assert();

  Guard<fbl::Mutex> guard{get_lock()};
  report_ = nullptr;
  arch_context_ = nullptr;
}
