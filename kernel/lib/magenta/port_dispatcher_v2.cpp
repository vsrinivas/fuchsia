// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/port_dispatcher_v2.h>

#include <assert.h>
#include <err.h>
#include <new.h>

#include <magenta/syscalls/port.h>

#include <kernel/auto_lock.h>

constexpr mx_rights_t kDefaultIOPortRightsV2 =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

mx_status_t PortDispatcherV2::Create(uint32_t options,
                                   mxtl::RefPtr<Dispatcher>* dispatcher,
                                   mx_rights_t* rights) {
    DEBUG_ASSERT(options == MX_PORT_OPT_V2);
    AllocChecker ac;
    auto disp = new (&ac) PortDispatcherV2(options);
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultIOPortRightsV2;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

PortDispatcherV2::PortDispatcherV2(uint32_t /*options*/)
    : event_(EVENT_FLAG_AUTOUNSIGNAL) {
}

PortDispatcherV2::~PortDispatcherV2() {
    while (!packets_.is_empty()) {
        auto packet = packets_.pop_front();
        if (packet->from_heap)
            delete packet;
    }
}

void PortDispatcherV2::on_zero_handles() {
}

mx_status_t PortDispatcherV2::Queue(PortPacket* packet) {
    int wake_count = 0;
    {
        AutoLock al(&lock_);
        packets_.push_back(packet);
        wake_count = event_.Signal();
    }

    if (wake_count)
        thread_preempt(false);

    return NO_ERROR;
}

mx_status_t PortDispatcherV2::DeQueue(mx_time_t timeout, PortPacket** packet) {
    while (true) {
        {
            AutoLock al(&lock_);
            if (!packets_.is_empty()) {
                *packet = packets_.pop_front();
                return NO_ERROR;
            }
        }

        if (timeout == 0ull)
            return ERR_TIMED_OUT;

        lk_time_t to = mx_time_to_lk(timeout);
        status_t st = event_.Wait((to == 0u) ? 1u : to);
        if (st != NO_ERROR)
            return st;
    }
    return ERR_BAD_STATE;
}
