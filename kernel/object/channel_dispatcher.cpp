// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/channel_dispatcher.h>

#include <string.h>

#include <assert.h>
#include <err.h>
#include <trace.h>

#include <lib/counters.h>
#include <kernel/event.h>
#include <platform.h>
#include <object/handle.h>
#include <object/message_packet.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/type_support.h>
#include <zircon/rights.h>
#include <zircon/types.h>

using fbl::AutoLock;

#define LOCAL_TRACE 0

KCOUNTER(channel_packet_depth_1, "kernel.channel.depth.1");
KCOUNTER(channel_packet_depth_4, "kernel.channel.depth.4");
KCOUNTER(channel_packet_depth_16, "kernel.channel.depth.16");
KCOUNTER(channel_packet_depth_64, "kernel.channel.depth.64");
KCOUNTER(channel_packet_depth_256, "kernel.channel.depth.256");
KCOUNTER(channel_packet_depth_unbounded, "kernel.channel.depth.unbounded");

// static
zx_status_t ChannelDispatcher::Create(fbl::RefPtr<Dispatcher>* dispatcher0,
                                      fbl::RefPtr<Dispatcher>* dispatcher1,
                                      zx_rights_t* rights) {
    fbl::AllocChecker ac;
    auto holder0 = fbl::AdoptRef(new (&ac) PeerHolder<ChannelDispatcher>());
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;
    auto holder1 = holder0;

    auto ch0 = fbl::AdoptRef(new (&ac) ChannelDispatcher(fbl::move(holder0)));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    auto ch1 = fbl::AdoptRef(new (&ac) ChannelDispatcher(fbl::move(holder1)));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    ch0->Init(ch1);
    ch1->Init(ch0);

    *rights = ZX_DEFAULT_CHANNEL_RIGHTS;
    *dispatcher0 = fbl::move(ch0);
    *dispatcher1 = fbl::move(ch1);
    return ZX_OK;
}

ChannelDispatcher::ChannelDispatcher(fbl::RefPtr<PeerHolder<ChannelDispatcher>> holder)
    : PeeredDispatcher(fbl::move(holder), ZX_CHANNEL_WRITABLE) {
}

// This is called before either ChannelDispatcher is accessible from threads other than the one
// initializing the channel, so it does not need locking.
void ChannelDispatcher::Init(fbl::RefPtr<ChannelDispatcher> other) TA_NO_THREAD_SAFETY_ANALYSIS {
    peer_ = fbl::move(other);
    peer_koid_ = peer_->get_koid();
}

ChannelDispatcher::~ChannelDispatcher() {
    // At this point the other endpoint no longer holds
    // a reference to us, so we can be sure we're discarding
    // any remaining messages safely.

    // It's not possible to do this safely in on_zero_handles()

    messages_.clear();
    message_count_ = 0;

    switch (max_message_count_) {
    case 0 ... 1:
        kcounter_add(channel_packet_depth_1, 1u);
        break;
    case 2 ... 4:
        kcounter_add(channel_packet_depth_4, 1u);
        break;
    case 5 ... 16:
        kcounter_add(channel_packet_depth_16, 1u);
        break;
    case 17 ... 64:
        kcounter_add(channel_packet_depth_64, 1u);
        break;
    case 65 ... 256:
        kcounter_add(channel_packet_depth_256, 1u);
        break;
    default:
        kcounter_add(channel_packet_depth_unbounded, 1u);
        break;
    }
}

zx_status_t ChannelDispatcher::add_observer(StateObserver* observer) {
    canary_.Assert();

    AutoLock lock(get_lock());
    StateObserver::CountInfo cinfo =
        {{{message_count_, ZX_CHANNEL_READABLE}, {0u, 0u}}};
    AddObserverLocked(observer, &cinfo);
    return ZX_OK;
}

void ChannelDispatcher::RemoveWaiter(MessageWaiter* waiter) {
    AutoLock lock(get_lock());
    if (!waiter->InContainer()) {
        return;
    }
    waiters_.erase(*waiter);
}

void ChannelDispatcher::on_zero_handles() {
    canary_.Assert();

    AutoLock lock(get_lock());
    // Detach other endpoint

    fbl::RefPtr<ChannelDispatcher> other = fbl::move(peer_);

    // (3A) Abort any waiting Call operations
    // because we've been canceled by reason
    // of our local handle going away.
    // Remove waiter from list.
    while (!waiters_.is_empty()) {
        auto waiter = waiters_.pop_front();
        waiter->Cancel(ZX_ERR_CANCELED);
    }

    // Ensure other endpoint detaches us
    if (other)
        other->OnPeerZeroHandlesLocked();
}

// This requires holding the shared channel lock. The thread analysis
// can reason about repeated calls to get_lock() on the shared object,
// but cannot reason about the aliasing between left->get_lock() and
// right->get_lock(), which occurs above in on_zero_handles.
void ChannelDispatcher::OnPeerZeroHandlesLocked() TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    peer_.reset();
    UpdateStateLocked(ZX_CHANNEL_WRITABLE, ZX_CHANNEL_PEER_CLOSED);
    // (3B) Abort any waiting Call operations
    // because we've been canceled by reason
    // of the opposing endpoint going away.
    // Remove waiter from list.
    while (!waiters_.is_empty()) {
        auto waiter = waiters_.pop_front();
        waiter->Cancel(ZX_ERR_PEER_CLOSED);
    }
}

zx_status_t ChannelDispatcher::Read(uint32_t* msg_size,
                                    uint32_t* msg_handle_count,
                                    fbl::unique_ptr<MessagePacket>* msg,
                                    bool may_discard) {
    canary_.Assert();

    auto max_size = *msg_size;
    auto max_handle_count = *msg_handle_count;

    AutoLock lock(get_lock());

    if (messages_.is_empty())
        return peer_ ? ZX_ERR_SHOULD_WAIT : ZX_ERR_PEER_CLOSED;

    *msg_size = messages_.front().data_size();
    *msg_handle_count = messages_.front().num_handles();
    zx_status_t rv = ZX_OK;
    if (*msg_size > max_size || *msg_handle_count > max_handle_count) {
        if (!may_discard)
            return ZX_ERR_BUFFER_TOO_SMALL;
        rv = ZX_ERR_BUFFER_TOO_SMALL;
    }

    *msg = messages_.pop_front();
    message_count_--;

    if (messages_.is_empty())
        UpdateStateLocked(ZX_CHANNEL_READABLE, 0u);

    return rv;
}

zx_status_t ChannelDispatcher::Write(fbl::unique_ptr<MessagePacket> msg) {
    canary_.Assert();

    AutoLock lock(get_lock());
    if (!peer_) {
        // |msg| will be destroyed but we want to keep the handles alive since
        // the caller should put them back into the process table.
        msg->set_owns_handles(false);
        return ZX_ERR_PEER_CLOSED;
    }

    if (peer_->WriteSelf(fbl::move(msg)) > 0)
        thread_reschedule();

    return ZX_OK;
}

zx_status_t ChannelDispatcher::Call(fbl::unique_ptr<MessagePacket> msg,
                                    zx_time_t deadline, bool* return_handles,
                                    fbl::unique_ptr<MessagePacket>* reply) {

    canary_.Assert();

    auto waiter = ThreadDispatcher::GetCurrent()->GetMessageWaiter();
    if (unlikely(waiter->BeginWait(fbl::WrapRefPtr(this), msg->get_txid()) != ZX_OK)) {
        // If a thread tries BeginWait'ing twice, the VDSO contract around retrying
        // channel calls has been violated.  Shoot the misbehaving process.
        ProcessDispatcher::GetCurrent()->Kill();
        return ZX_ERR_BAD_STATE;
    }

    {
        AutoLock lock(get_lock());

        if (!peer_) {
            // |msg| will be destroyed but we want to keep the handles alive since
            // the caller should put them back into the process table.
            msg->set_owns_handles(false);
            *return_handles = true;
            waiter->EndWait(reply);
            return ZX_ERR_PEER_CLOSED;
        }

        // (0) Before writing the outbound message and waiting, add our
        // waiter to the list.
        waiters_.push_back(waiter);

        // (1) Write outbound message to opposing endpoint.
        peer_->WriteSelf(fbl::move(msg));
    }

    // Reuse the code from the half-call used for retrying a Call after thread
    // suspend.
    return ResumeInterruptedCall(waiter, deadline, reply);
}

zx_status_t ChannelDispatcher::ResumeInterruptedCall(MessageWaiter* waiter,
                                                     zx_time_t deadline,
                                                     fbl::unique_ptr<MessagePacket>* reply) {
    canary_.Assert();

    // (2) Wait for notification via waiter's event or for the
    // deadline to hit.
    zx_status_t status = waiter->Wait(deadline);
    if (status == ZX_ERR_INTERNAL_INTR_RETRY) {
        // If we got interrupted, return out to usermode, but
        // do not clear the waiter.
        return status;
    }

    // (3) see (3A), (3B) above or (3C) below for paths where
    // the waiter could be signaled and removed from the list.
    //
    // If the deadline hits, the waiter is not removed
    // from the list *but* another thread could still
    // cause (3A), (3B), or (3C) before the lock below.
    {
        AutoLock lock(get_lock());

        // (4) If any of (3A), (3B), or (3C) have occurred,
        // we were removed from the waiters list already
        // and EndWait() returns a non-ZX_ERR_TIMED_OUT status.
        // Otherwise, the status is ZX_ERR_TIMED_OUT and it
        // is our job to remove the waiter from the list.
        if ((status = waiter->EndWait(reply)) == ZX_ERR_TIMED_OUT)
            waiters_.erase(*waiter);
    }

    return status;
}

size_t ChannelDispatcher::TxMessageMax() const {
    return SIZE_MAX;
}

int ChannelDispatcher::WriteSelf(fbl::unique_ptr<MessagePacket> msg) {
    canary_.Assert();

    if (!waiters_.is_empty()) {
        // If the far side is waiting for replies to messages
        // send via "call", see if this message has a matching
        // txid to one of the waiters, and if so, deliver it.
        zx_txid_t txid = msg->get_txid();
        for (auto& waiter: waiters_) {
            // (3C) Deliver message to waiter.
            // Remove waiter from list.
            if (waiter.get_txid() == txid) {
                waiters_.erase(waiter);
                // we return how many threads have been woken up, or zero.
                return waiter.Deliver(fbl::move(msg));
            }
        }
    }
    messages_.push_back(fbl::move(msg));
    message_count_++;
    if (message_count_ > max_message_count_) {
        max_message_count_ = message_count_;
    }

    UpdateStateLocked(0u, ZX_CHANNEL_READABLE);
    return 0;
}

zx_status_t ChannelDispatcher::UserSignalSelf(uint32_t clear_mask, uint32_t set_mask) {
    canary_.Assert();
    UpdateStateLocked(clear_mask, set_mask);
    return ZX_OK;
}

ChannelDispatcher::MessageWaiter::~MessageWaiter() {
    if (unlikely(channel_)) {
        channel_->RemoveWaiter(this);
    }
    DEBUG_ASSERT(!InContainer());
}

zx_status_t ChannelDispatcher::MessageWaiter::BeginWait(fbl::RefPtr<ChannelDispatcher> channel,
                                                        zx_txid_t txid) {
    if (unlikely(channel_)) {
        return ZX_ERR_BAD_STATE;
    }
    DEBUG_ASSERT(!InContainer());

    txid_ = txid;
    status_ = ZX_ERR_TIMED_OUT;
    channel_ = fbl::move(channel);
    event_.Unsignal();
    return ZX_OK;
}

int ChannelDispatcher::MessageWaiter::Deliver(fbl::unique_ptr<MessagePacket> msg) {
    DEBUG_ASSERT(channel_);

    msg_ = fbl::move(msg);
    status_ = ZX_OK;
    return event_.Signal(ZX_OK);
}

int ChannelDispatcher::MessageWaiter::Cancel(zx_status_t status) {
    DEBUG_ASSERT(!InContainer());
    DEBUG_ASSERT(channel_);
    status_ = status;
    return event_.Signal(status);
}

zx_status_t ChannelDispatcher::MessageWaiter::Wait(zx_time_t deadline) {
    if (unlikely(!channel_)) {
        return ZX_ERR_BAD_STATE;
    }
    return event_.Wait(deadline);
}

// Returns any delivered message via out and the status.
zx_status_t ChannelDispatcher::MessageWaiter::EndWait(fbl::unique_ptr<MessagePacket>* out) {
    if (unlikely(!channel_)) {
        return ZX_ERR_BAD_STATE;
    }
    *out = fbl::move(msg_);
    channel_ = nullptr;
    return status_;
}
