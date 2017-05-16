// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/log_dispatcher.h>
#include <magenta/syscalls/log.h>

#include <err.h>

#include <mxalloc/new.h>

constexpr mx_rights_t kDefaultEventRights =
    MX_RIGHT_TRANSFER | MX_RIGHT_WRITE | MX_RIGHT_DUPLICATE;

status_t LogDispatcher::Create(uint32_t flags, mxtl::RefPtr<Dispatcher>* dispatcher,
                               mx_rights_t* rights) {
    AllocChecker ac;
    auto disp = new (&ac) LogDispatcher(flags);
    if (!ac.check()) return ERR_NO_MEMORY;

    if (flags & MX_LOG_FLAG_READABLE) {
        dlog_reader_init(&disp->reader_, &LogDispatcher::Notify, disp);
    }

    *rights = kDefaultEventRights;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

LogDispatcher::LogDispatcher(uint32_t flags)
    : flags_(flags), state_tracker_(MX_LOG_WRITABLE) {
}

LogDispatcher::~LogDispatcher() {
    if (flags_ & MX_LOG_FLAG_READABLE) {
        dlog_reader_destroy(&reader_);
    }
}

void LogDispatcher::Signal() {
    canary_.Assert();

    AutoLock lock(&lock_);
    state_tracker_.UpdateState(0, MX_CHANNEL_READABLE);
}

// static
void LogDispatcher::Notify(void* cookie) {
    LogDispatcher* log = static_cast<LogDispatcher*>(cookie);
    log->Signal();
}

status_t LogDispatcher::Write(uint32_t flags, const void* ptr, size_t len) {
    canary_.Assert();

    return dlog_write(flags_, ptr, len);
}

status_t LogDispatcher::Read(uint32_t flags, void* ptr, size_t len, size_t* actual) {
    canary_.Assert();

    if (!(flags_ & MX_LOG_FLAG_READABLE))
        return ERR_BAD_STATE;

    AutoLock lock(&lock_);

    mx_status_t status = dlog_read(&reader_, 0, ptr, len, actual);
    if (status == ERR_SHOULD_WAIT) {
        state_tracker_.UpdateState(MX_CHANNEL_READABLE, 0);
    }

    return status;
}

