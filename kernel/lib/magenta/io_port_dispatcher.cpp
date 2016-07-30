// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/io_port_dispatcher.h>

#include <assert.h>
#include <err.h>
#include <new.h>

#include <kernel/auto_lock.h>

#include <magenta/state_tracker.h>

static_assert(sizeof(mx_user_packet_t) == sizeof(mx_io_packet_t), "packet size mismatch");


constexpr mx_rights_t kDefaultIOPortRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

mx_status_t IOPortDispatcher::Create(uint32_t options,
                                     utils::RefPtr<Dispatcher>* dispatcher,
                                     mx_rights_t* rights) {
    AllocChecker ac;
    auto disp = new (&ac) IOPortDispatcher(options);
    if (!ac.check())
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
    // The observers hold a ref to the dispatcher so if the dispatcher
    // is being destroyed there should be no registered observers.
    DEBUG_ASSERT(observers_.is_empty());

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

mx_status_t IOPortDispatcher::Bind(Handle* handle, mx_signals_t signals, uint64_t key) {
    // This method is called under the handle table lock.
    auto state_tracker = handle->dispatcher()->get_state_tracker();
    if (!state_tracker || !state_tracker->is_waitable())
        return ERR_NOT_SUPPORTED;

    AllocChecker ac;
    auto observer  =
        new (&ac) IOPortObserver(utils::RefPtr<IOPortDispatcher>(this), handle, signals, key);
    if (!ac.check())
        return ERR_NO_MEMORY;

    {
        // TODO(cpu) : Currently we allow duplicated handle / key. This is bug MG-227.
        AutoLock al(&lock_);
        observers_.push_front(observer);
    }

    mx_status_t result = state_tracker->AddObserver(observer);
    if (result != NO_ERROR) {
        CancelObserver(observer);
        return result;
    }

    return NO_ERROR;
}

mx_status_t IOPortDispatcher::Unbind(Handle* handle, uint64_t key) {
    // This method is called under the handle table lock.
    IOPortObserver* observer = nullptr;
    {
        AutoLock al(&lock_);

        observer = observers_.find_if([handle, key](const IOPortObserver& ob) {
            return ((handle == ob.get_handle()) && (key == ob.get_key()));
        });

        if (!observer)
            return ERR_BAD_HANDLE;

        // This code could 'race' with IOPortObserver::OnCancel() so the atomic
        // SetState() ensures that either the rest of this function executes
        // or the OnDidCancel() + CancelObserver() executes.
        if (observer->SetState(IOPortObserver::UNBOUND) != IOPortObserver::NEW)
            return NO_ERROR;

        observers_.erase(*observer);
    }

    auto dispatcher = handle->dispatcher();
    dispatcher->get_state_tracker()->RemoveObserver(observer);
    delete observer;
    return NO_ERROR;
}

void IOPortDispatcher::CancelObserver(IOPortObserver* observer) {
    {
        AutoLock al(&lock_);
        observers_.erase(*observer);
    }

    delete observer;
}
