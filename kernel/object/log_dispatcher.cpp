// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/log_dispatcher.h>

#include <zircon/rights.h>
#include <zircon/syscalls/log.h>

#include <err.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

zx_status_t LogDispatcher::Create(uint32_t flags, fbl::RefPtr<Dispatcher>* dispatcher,
                                  zx_rights_t* rights) {
    fbl::AllocChecker ac;
    auto disp = new (&ac) LogDispatcher(flags);
    if (!ac.check()) return ZX_ERR_NO_MEMORY;

    if (flags & ZX_LOG_FLAG_READABLE) {
        dlog_reader_init(&disp->reader_, &LogDispatcher::Notify, disp);
    }

    *rights = ZX_DEFAULT_LOG_RIGHTS;
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

LogDispatcher::LogDispatcher(uint32_t flags)
    : SoloDispatcher(ZX_LOG_WRITABLE), flags_(flags) {
}

LogDispatcher::~LogDispatcher() {
    if (flags_ & ZX_LOG_FLAG_READABLE) {
        dlog_reader_destroy(&reader_);
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

zx_status_t LogDispatcher::Write(uint32_t flags, const void* ptr, size_t len) {
    canary_.Assert();

    return dlog_write(flags_ | flags, ptr, len);
}

zx_status_t LogDispatcher::Read(uint32_t flags, void* ptr, size_t len, size_t* actual) {
    canary_.Assert();

    if (!(flags_ & ZX_LOG_FLAG_READABLE))
        return ZX_ERR_BAD_STATE;

    fbl::AutoLock lock(get_lock());

    zx_status_t status = dlog_read(&reader_, 0, ptr, len, actual);
    if (status == ZX_ERR_SHOULD_WAIT) {
        UpdateStateLocked(ZX_CHANNEL_READABLE, 0);
    }

    return status;
}
