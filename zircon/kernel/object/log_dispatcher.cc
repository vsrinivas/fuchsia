// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/log_dispatcher.h"

#include <lib/counters.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/syscalls/log.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

KCOUNTER(dispatcher_log_create_count, "dispatcher.log.create")
KCOUNTER(dispatcher_log_destroy_count, "dispatcher.log.destroy")

zx_status_t LogDispatcher::Create(uint32_t flags, KernelHandle<LogDispatcher>* handle,
                                  zx_rights_t* rights) {
  fbl::AllocChecker ac;
  KernelHandle new_handle(fbl::AdoptRef(new (&ac) LogDispatcher(flags)));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  if (flags & ZX_LOG_FLAG_READABLE) {
    // Thread safety analysis is disabled here. Calling Initialize could
    // immediately call Notify, calling back into the dispatcher. Thus the lock
    // cannot be held. So far, the log dispatcher holding |reader_| has not
    // escaped beyond this thread, so it is safe to call Initialize.
    [&]() TA_NO_THREAD_SAFETY_ANALYSIS {
      new_handle.dispatcher()->reader_.Initialize(&LogDispatcher::Notify,
                                                  new_handle.dispatcher().get());
    }();
  }

  // Note: ZX_RIGHT_READ is added by sys_debuglog_create when ZX_LOG_FLAG_READABLE.
  *rights = default_rights();
  *handle = ktl::move(new_handle);
  return ZX_OK;
}

LogDispatcher::LogDispatcher(uint32_t flags) : SoloDispatcher(ZX_LOG_WRITABLE), flags_(flags) {
  kcounter_add(dispatcher_log_create_count, 1);
}

LogDispatcher::~LogDispatcher() {
  kcounter_add(dispatcher_log_destroy_count, 1);

  if (flags_ & ZX_LOG_FLAG_READABLE) {
    reader_.Disconnect();
  }
}

void LogDispatcher::Signal() {
  canary_.Assert();

  UpdateState(0, ZX_CHANNEL_READABLE);
}

// static
void LogDispatcher::Notify(void* cookie) {
  LogDispatcher* log = static_cast<LogDispatcher*>(cookie);
  log->Signal();
}

zx_status_t LogDispatcher::Write(uint32_t severity, uint32_t flags, ktl::string_view str) {
  canary_.Assert();

  return dlog_write(severity, flags_ | flags, str);
}

zx_status_t LogDispatcher::Read(uint32_t flags, void* ptr, size_t len, size_t* actual) {
  canary_.Assert();

  if (!(flags_ & ZX_LOG_FLAG_READABLE))
    return ZX_ERR_BAD_STATE;

  Guard<Mutex> guard{get_lock()};

  zx_status_t status = reader_.Read(0, ptr, len, actual);
  if (status == ZX_ERR_SHOULD_WAIT) {
    UpdateStateLocked(ZX_CHANNEL_READABLE, 0);
  }

  return status;
}
