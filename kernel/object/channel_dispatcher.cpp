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

#include <kernel/event.h>
#include <platform.h>
#include <object/handle.h>
#include <object/message_packet.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

#include <magenta/rights.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/type_support.h>

using fbl::AutoLock;

#define LOCAL_TRACE 0

// static
mx_status_t ChannelDispatcher::Create(fbl::RefPtr<Dispatcher>* dispatcher0,
                                      fbl::RefPtr<Dispatcher>* dispatcher1,
                                      mx_rights_t* rights) {
    fbl::AllocChecker ac;
    auto ch0 = fbl::AdoptRef(new (&ac) ChannelDispatcher());
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    auto ch1 = fbl::AdoptRef(new (&ac) ChannelDispatcher());
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    ch0->Init(ch1);
    ch1->Init(ch0);

    *rights = MX_DEFAULT_CHANNEL_RIGHTS;
    *dispatcher0 = fbl::move(ch0);
    *dispatcher1 = fbl::move(ch1);
    return MX_OK;
}

ChannelDispatcher::ChannelDispatcher()
    : state_tracker_(MX_CHANNEL_WRITABLE) {
}

// This is called before either ChannelDispatcher is accessible from threads other than the one
// initializing the channel, so it does not need locking.
void ChannelDispatcher::Init(fbl::RefPtr<ChannelDispatcher> other) TA_NO_THREAD_SAFETY_ANALYSIS {
    other_ = fbl::move(other);
    other_koid_ = other_->get_koid();
}

ChannelDispatcher::~ChannelDispatcher() {
    // At this point the other endpoint no longer holds
    // a reference to us, so we can be sure we're discarding
    // any remaining messages safely.

    // It's not possible to do this safely in on_zero_handles()

    messages_.clear();
}

mx_status_t ChannelDispatcher::add_observer(StateObserver* observer) {
    canary_.Assert();

    AutoLock lock(&lock_);
    StateObserver::CountInfo cinfo =
        {{{messages_.size_slow(), MX_CHANNEL_READABLE}, {0u, 0u}}};
    state_tracker_.AddObserver(observer, &cinfo);
    return MX_OK;
}

void ChannelDispatcher::RemoveWaiter(MessageWaiter* waiter) {
    AutoLock lock(&lock_);
    if (!waiter->InContainer()) {
        return;
    }
    waiters_.erase(*waiter);
}

void ChannelDispatcher::on_zero_handles() {
    canary_.Assert();

    // Detach other endpoint
    fbl::RefPtr<ChannelDispatcher> other;
    {
        AutoLock lock(&lock_);
        other = fbl::move(other_);

        // (3A) Abort any waiting Call operations
        // because we've been canceled by reason
        // of our local handle going away.
        // Remove waiter from list.
        while (!waiters_.is_empty()) {
            auto waiter = waiters_.pop_front();
            waiter->Cancel(MX_ERR_CANCELED);
        }
    }

    // Ensure other endpoint detaches us
    if (other)
        other->OnPeerZeroHandles();
}

void ChannelDispatcher::OnPeerZeroHandles() {
    canary_.Assert();

    AutoLock lock(&lock_);
    other_.reset();
    state_tracker_.UpdateState(MX_CHANNEL_WRITABLE, MX_CHANNEL_PEER_CLOSED);
    // (3B) Abort any waiting Call operations
    // because we've been canceled by reason
    // of the opposing endpoint going away.
    // Remove waiter from list.
    while (!waiters_.is_empty()) {
        auto waiter = waiters_.pop_front();
        waiter->Cancel(MX_ERR_PEER_CLOSED);
    }
}

mx_status_t ChannelDispatcher::Read(uint32_t* msg_size,
                                    uint32_t* msg_handle_count,
                                    fbl::unique_ptr<MessagePacket>* msg,
                                    bool may_discard) {
    canary_.Assert();

    auto max_size = *msg_size;
    auto max_handle_count = *msg_handle_count;

    AutoLock lock(&lock_);

    if (messages_.is_empty())
        return other_ ? MX_ERR_SHOULD_WAIT : MX_ERR_PEER_CLOSED;

    *msg_size = messages_.front().data_size();
    *msg_handle_count = messages_.front().num_handles();
    mx_status_t rv = MX_OK;
    if (*msg_size > max_size || *msg_handle_count > max_handle_count) {
        if (!may_discard)
            return MX_ERR_BUFFER_TOO_SMALL;
        rv = MX_ERR_BUFFER_TOO_SMALL;
    }

    *msg = messages_.pop_front();

    if (messages_.is_empty())
        state_tracker_.UpdateState(MX_CHANNEL_READABLE, 0u);

    return rv;
}

mx_status_t ChannelDispatcher::Write(fbl::unique_ptr<MessagePacket> msg) {
    canary_.Assert();

    fbl::RefPtr<ChannelDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_) {
            // |msg| will be destroyed but we want to keep the handles alive since
            // the caller should put them back into the process table.
            msg->set_owns_handles(false);
            return MX_ERR_PEER_CLOSED;
        }
        other = other_;
    }

    if (other->WriteSelf(fbl::move(msg)) > 0)
        thread_reschedule();

    return MX_OK;
}

mx_status_t ChannelDispatcher::Call(fbl::unique_ptr<MessagePacket> msg,
                                    mx_time_t deadline, bool* return_handles,
                                    fbl::unique_ptr<MessagePacket>* reply) {

    canary_.Assert();

    auto waiter = ThreadDispatcher::GetCurrent()->GetMessageWaiter();
    if (unlikely(waiter->BeginWait(fbl::WrapRefPtr(this), msg->get_txid()) != MX_OK)) {
        // If a thread tries BeginWait'ing twice, the VDSO contract around retrying
        // channel calls has been violated.  Shoot the misbehaving process.
        ProcessDispatcher::GetCurrent()->Kill();
        return MX_ERR_BAD_STATE;
    }

    fbl::RefPtr<ChannelDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_) {
            // |msg| will be destroyed but we want to keep the handles alive since
            // the caller should put them back into the process table.
            msg->set_owns_handles(false);
            *return_handles = true;
            waiter->EndWait(reply);
            return MX_ERR_PEER_CLOSED;
        }
        other = other_;

        // (0) Before writing the outbound message and waiting, add our
        // waiter to the list.
        waiters_.push_back(waiter);
    }

    // (1) Write outbound message to opposing endpoint.
    other->WriteSelf(fbl::move(msg));

    // Reuse the code from the half-call used for retrying a Call after thread
    // suspend.
    return ResumeInterruptedCall(waiter, deadline, reply);
}

mx_status_t ChannelDispatcher::ResumeInterruptedCall(MessageWaiter* waiter,
                                                     mx_time_t deadline,
                                                     fbl::unique_ptr<MessagePacket>* reply) {
    canary_.Assert();

    // (2) Wait for notification via waiter's event or for the
    // deadline to hit.
    mx_status_t status = waiter->Wait(deadline);
    if (status == MX_ERR_INTERNAL_INTR_RETRY) {
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
        AutoLock lock(&lock_);

        // (4) If any of (3A), (3B), or (3C) have occurred,
        // we were removed from the waiters list already
        // and EndWait() returns a non-MX_ERR_TIMED_OUT status.
        // Otherwise, the status is MX_ERR_TIMED_OUT and it
        // is our job to remove the waiter from the list.
        if ((status = waiter->EndWait(reply)) == MX_ERR_TIMED_OUT)
            waiters_.erase(*waiter);
    }

    return status;
}

int ChannelDispatcher::WriteSelf(fbl::unique_ptr<MessagePacket> msg) {
    canary_.Assert();

    AutoLock lock(&lock_);

    if (!waiters_.is_empty()) {
        // If the far side is waiting for replies to messages
        // send via "call", see if this message has a matching
        // txid to one of the waiters, and if so, deliver it.
        mx_txid_t txid = msg->get_txid();
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

    state_tracker_.UpdateState(0u, MX_CHANNEL_READABLE);
    return 0;
}

mx_status_t ChannelDispatcher::user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) {
    canary_.Assert();

    if ((set_mask & ~MX_USER_SIGNAL_ALL) || (clear_mask & ~MX_USER_SIGNAL_ALL))
        return MX_ERR_INVALID_ARGS;

    if (!peer) {
        state_tracker_.UpdateState(clear_mask, set_mask);
        return MX_OK;
    }

    fbl::RefPtr<ChannelDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_)
            return MX_ERR_PEER_CLOSED;
        other = other_;
    }

    return other->UserSignalSelf(clear_mask, set_mask);
}

mx_status_t ChannelDispatcher::UserSignalSelf(uint32_t clear_mask, uint32_t set_mask) {
    canary_.Assert();
    state_tracker_.UpdateState(clear_mask, set_mask);
    return MX_OK;
}

ChannelDispatcher::MessageWaiter::~MessageWaiter() {
    if (unlikely(channel_)) {
        channel_->RemoveWaiter(this);
    }
    DEBUG_ASSERT(!InContainer());
}

mx_status_t ChannelDispatcher::MessageWaiter::BeginWait(fbl::RefPtr<ChannelDispatcher> channel,
                                                        mx_txid_t txid) {
    if (unlikely(channel_)) {
        return MX_ERR_BAD_STATE;
    }
    DEBUG_ASSERT(!InContainer());

    txid_ = txid;
    status_ = MX_ERR_TIMED_OUT;
    channel_ = fbl::move(channel);
    event_.Unsignal();
    return MX_OK;
}

int ChannelDispatcher::MessageWaiter::Deliver(fbl::unique_ptr<MessagePacket> msg) {
    DEBUG_ASSERT(channel_);

    msg_ = fbl::move(msg);
    status_ = MX_OK;
    return event_.Signal(MX_OK);
}

int ChannelDispatcher::MessageWaiter::Cancel(mx_status_t status) {
    DEBUG_ASSERT(!InContainer());
    DEBUG_ASSERT(channel_);
    status_ = status;
    return event_.Signal(status);
}

mx_status_t ChannelDispatcher::MessageWaiter::Wait(lk_time_t deadline) {
    if (unlikely(!channel_)) {
        return MX_ERR_BAD_STATE;
    }
    return event_.Wait(deadline);
}

// Returns any delivered message via out and the status.
mx_status_t ChannelDispatcher::MessageWaiter::EndWait(fbl::unique_ptr<MessagePacket>* out) {
    if (unlikely(!channel_)) {
        return MX_ERR_BAD_STATE;
    }
    *out = fbl::move(msg_);
    channel_ = nullptr;
    return status_;
}
