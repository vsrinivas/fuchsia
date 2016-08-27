// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/log_dispatcher.h>

#include <err.h>
#include <new.h>

constexpr mx_rights_t kDefaultEventRights =
    MX_RIGHT_TRANSFER | MX_RIGHT_WRITE | MX_RIGHT_DUPLICATE;

status_t LogDispatcher::Create(uint32_t flags, mxtl::RefPtr<Dispatcher>* dispatcher,
                               mx_rights_t* rights) {
    AllocChecker ac;
    auto disp = new (&ac) LogDispatcher(flags);
    if (!ac.check()) return ERR_NO_MEMORY;

    *rights = kDefaultEventRights;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

LogDispatcher::LogDispatcher(uint32_t flags) : flags_(flags) {
    if (flags & MX_LOG_FLAG_READABLE) {
        dlog_reader_init(&reader_);
    }
}

LogDispatcher::~LogDispatcher() {
    if (flags_ & MX_LOG_FLAG_READABLE) {
        dlog_reader_destroy(&reader_);
    }
}

status_t LogDispatcher::Write(const void* ptr, size_t len, uint32_t flags) {
    return dlog_write(flags_, ptr, len);
}

status_t LogDispatcher::Read(void* ptr, size_t len, uint32_t flags) {
    if (flags_ & MX_LOG_FLAG_READABLE) {
        return dlog_read(&reader_, 0, ptr, len);
    } else {
        return ERR_BAD_STATE;
    }
}

status_t LogDispatcher::ReadFromUser(void* ptr, size_t len, uint32_t flags) {
    if (!(flags_ & MX_LOG_FLAG_READABLE)) {
        return ERR_BAD_STATE;
    }
    for (;;) {
        mx_status_t r = dlog_read_user(&reader_, 0, ptr, len);
        if ((r == ERR_BAD_STATE) && (flags & MX_LOG_FLAG_WAIT)) {
            dlog_wait(&reader_);
            continue;
        }
        return r;
    }
}
