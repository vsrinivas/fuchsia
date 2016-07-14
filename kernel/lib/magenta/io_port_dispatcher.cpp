// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/io_port_dispatcher.h>

#include <err.h>
#include <kernel/auto_lock.h>

static_assert(sizeof(mx_user_packet_t) == sizeof(mx_io_packet_t), "packet size mismatch");


constexpr mx_rights_t kDefaultIOPortRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

mx_status_t IOPortDispatcher::Create(uint32_t options,
                                     utils::RefPtr<Dispatcher>* dispatcher,
                                     mx_rights_t* rights) {
    auto disp = new IOPortDispatcher(options);
    if (!disp)
        return ERR_NO_MEMORY;

    uint32_t depth = options == MX_IOPORT_OPT_1K_SLOTS ? 1024 : 128;

    status_t st = disp->Init(depth);
    if (st < 0)
        return st;

    *rights = kDefaultIOPortRights;
    *dispatcher = utils::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

IOPortDispatcher::IOPortDispatcher(uint32_t options) : options_(options) {
    mutex_init(&lock_);
    event_init(&event_, false, EVENT_FLAG_AUTOUNSIGNAL);
}

IOPortDispatcher::~IOPortDispatcher() {
    event_destroy(&event_);
    mutex_destroy(&lock_);
}

mx_status_t IOPortDispatcher::Init(uint32_t depth) {
    if (!packets_.Init(depth))
        return ERR_NO_MEMORY;
    return NO_ERROR;
}

mx_status_t IOPortDispatcher::Queue(const IOP_Packet* packet) {
    int wake_count = 0;
    mx_status_t status = NO_ERROR;
    {
        AutoLock al(&lock_);
        auto tail = packets_.push_tail();
        if (!tail) {
            status = ERR_NOT_ENOUGH_BUFFER;
        } else {
            *tail = *packet;
        }
        wake_count = event_signal_etc(&event_, false, status);
    }

    if (wake_count)
        thread_yield();

    return status;
}

mx_status_t IOPortDispatcher::Wait(IOP_Packet* packet) {
    IOP_Packet* head;
    while (true) {
        {
            AutoLock al(&lock_);
            head = packets_.pop_head();

            if (head) {
                *packet = *head;
                return NO_ERROR;
            }
        }
        status_t st = event_wait_timeout(&event_, INFINITE_TIME, true);
        if (st != NO_ERROR)
            return st;
    }
}

