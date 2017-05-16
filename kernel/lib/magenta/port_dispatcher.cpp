// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/port_dispatcher.h>

#include <assert.h>
#include <err.h>

#include <arch/ops.h>
#include <arch/user_copy.h>

#include <kernel/auto_lock.h>
#include <lib/user_copy.h>
#include <platform.h>

#include <magenta/excp_port.h>
#include <magenta/state_tracker.h>
#include <magenta/user_copy.h>

#include <mxalloc/new.h>
#include <mxcpp/new.h>

constexpr mx_rights_t kDefaultIOPortRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

IOP_Packet* IOP_Packet::Alloc(size_t size) {
    AllocChecker ac;
    auto mem = new (&ac) char [sizeof(IOP_Packet) + size];
    if (!ac.check())
        return nullptr;
    return new (mem) IOP_Packet(size);
}

IOP_Packet* IOP_Packet::Make(const void* data, size_t size) {
    auto pk = Alloc(size);
    if (!pk)
        return nullptr;
    memcpy(reinterpret_cast<char*>(pk) + sizeof(IOP_Packet), data, size);
    return pk;
}

IOP_Packet* IOP_Packet::MakeFromUser(const void* data, size_t size) {
    auto pk = Alloc(size);
    if (!pk)
        return nullptr;

    auto header = reinterpret_cast<mx_packet_header_t*>(
        reinterpret_cast<char*>(pk) + sizeof(IOP_Packet));

    auto status = magenta_copy_from_user(data, header, size);
    header->type = MX_PORT_PKT_TYPE_USER;

    return (status == NO_ERROR) ? pk : nullptr;
}

void IOP_Packet::Delete(IOP_Packet* packet) {
    if (!packet || packet->is_signal)
        return;
    packet->~IOP_Packet();
    delete [] reinterpret_cast<char*>(packet);
}

bool IOP_Packet::CopyToUser(void* data, size_t* size) {
    if (*size < data_size)
        return ERR_BUFFER_TOO_SMALL;
    *size = data_size;
    return copy_to_user_unsafe(
        data, reinterpret_cast<char*>(this) + sizeof(IOP_Packet), data_size) == NO_ERROR;
}

IOP_Signal::IOP_Signal(uint64_t key, mx_signals_t signal)
    : IOP_Packet(sizeof(payload), true),
      payload {{key, MX_PORT_PKT_TYPE_IOSN, 0u}, 0u, 0u, signal, 0u},
      count(1u) {
}

mx_status_t PortDispatcher::Create(uint32_t options,
                                   mxtl::RefPtr<Dispatcher>* dispatcher,
                                   mx_rights_t* rights) {
    AllocChecker ac;
    auto disp = new (&ac) PortDispatcher(options);
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultIOPortRights;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

PortDispatcher::PortDispatcher(uint32_t /*options*/)
    : no_clients_(false) {
    event_init(&event_, false, 0);
}

PortDispatcher::~PortDispatcher() {
    FreePacketsLocked();
    DEBUG_ASSERT(packets_.is_empty());
    DEBUG_ASSERT(eports_.is_empty());
    event_destroy(&event_);
}

void PortDispatcher::FreePacketsLocked() {
    while (!packets_.is_empty()) {
        IOP_Packet::Delete(packets_.pop_front());
    }
    while (!at_zero_.is_empty()) {
        auto signal = at_zero_.pop_front();
        signal->is_signal = false;
        IOP_Packet::Delete(signal);
    }
}

void PortDispatcher::on_zero_handles() {
    canary_.Assert();

    AutoLock al(&lock_);
    no_clients_ = true;
    FreePacketsLocked();

    // Unlink and unbind exception ports.
    while (!eports_.is_empty()) {
        auto eport = eports_.pop_back();

        // Tell the eport to unbind itself, then drop our ref to it.
        lock_.Release();  // The eport may call our ::UnlinkExceptionPort
        eport->OnPortZeroHandles();
        lock_.Acquire();
    }
}

mx_status_t PortDispatcher::Queue(IOP_Packet* packet) {
    canary_.Assert();

    int wake_count = 0;
    mx_status_t status = NO_ERROR;
    {
        AutoLock al(&lock_);
        if (no_clients_) {
            status = ERR_UNAVAILABLE;
        } else {
            packets_.push_back(packet);
            wake_count = event_signal_etc(&event_, false, status);
        }
    }

    if (status != NO_ERROR) {
        IOP_Packet::Delete(packet);
        return status;
    }

    if (wake_count)
        thread_preempt(false);

    return NO_ERROR;
}

void* PortDispatcher::Signal(void* cookie, uint64_t key, mx_signals_t signal) {
    canary_.Assert();

    IOP_Signal* node;
    int prev_count;

    if (!cookie) {
        DEBUG_ASSERT(signal);
        AllocChecker ac;
        node = new (&ac) IOP_Signal(key, signal);
        if (!ac.check())
            return nullptr;

        DEBUG_ASSERT(node->count == 1);
        prev_count = 0;
    } else {
        node = reinterpret_cast<IOP_Signal*>(cookie);
        prev_count = atomic_add(&node->count, 1);
        DEBUG_ASSERT(node->is_signal);
        DEBUG_ASSERT(node->payload.signals == signal);
        DEBUG_ASSERT(node->payload.hdr.key == key);
        DEBUG_ASSERT(node->payload.hdr.type == MX_PORT_PKT_TYPE_IOSN);
    }

    DEBUG_ASSERT(prev_count >= 0);

    int wake_count = 0;
    {
        AutoLock al(&lock_);

        if (prev_count == 0) {
            if (node->InContainer())
                at_zero_.erase(*node);
            packets_.push_back(node);
        }

        wake_count = event_signal_etc(&event_, false, NO_ERROR);
    }

    if (wake_count)
        thread_preempt(false);

    return node;
}

mx_status_t PortDispatcher::Wait(mx_time_t deadline, IOP_Packet** packet) {
    canary_.Assert();

    while (true) {
        {
            AutoLock al(&lock_);
            if (!packets_.is_empty()) {
                auto pk = packets_.pop_front();
                ASSERT(pk);

                if (!pk->is_signal) {
                    *packet = pk;
                } else {
                    auto signal = static_cast<IOP_Signal*>(pk);
                    auto prev = atomic_add(&signal->count, -1);

                    // sketchy assert below. Trying to get an early warning
                    // of unexpected values in the case of MG-520.
                    DEBUG_ASSERT((prev > 0) && (prev < 1000));
                    DEBUG_ASSERT(signal->payload.hdr.type == MX_PORT_PKT_TYPE_IOSN);

                    if (prev == 1)
                        at_zero_.push_back(signal);
                    else
                        packets_.push_back(signal);
                    *packet = signal;
                }

                return NO_ERROR;
            } else {
                // it's empty, unsignal the event
                event_unsignal(&event_);
            }
        }

        status_t st = event_wait_deadline(&event_, deadline, true);
        if (st != NO_ERROR)
            return st;
    }
}

void PortDispatcher::LinkExceptionPort(ExceptionPort* eport) {
    canary_.Assert();

    AutoLock al(&lock_);
    DEBUG_ASSERT_COND(eport->PortMatches(this, /* allow_null */ false));
    DEBUG_ASSERT(!eport->InContainer());
    eports_.push_back(mxtl::move(AdoptRef(eport)));
}

void PortDispatcher::UnlinkExceptionPort(ExceptionPort* eport) {
    canary_.Assert();

    AutoLock al(&lock_);
    DEBUG_ASSERT_COND(eport->PortMatches(this, /* allow_null */ true));
    if (eport->InContainer()) {
        eports_.erase(*eport);
    }
}
