// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/channel_dispatcher.h>

#include <string.h>

#include <assert.h>
#include <err.h>
#include <trace.h>

#include <kernel/event.h>
#include <platform.h>

#include <magenta/handle.h>
#include <magenta/message_packet.h>
#include <magenta/process_dispatcher.h>
#include <magenta/rights.h>
#include <magenta/user_thread.h>

#include <mxalloc/new.h>
#include <mxtl/type_support.h>

#define LOCAL_TRACE 0

// static
status_t ChannelDispatcher::Create(uint32_t flags,
                                   mxtl::RefPtr<Dispatcher>* dispatcher0,
                                   mxtl::RefPtr<Dispatcher>* dispatcher1,
                                   mx_rights_t* rights) {
    AllocChecker ac;
    auto ch0 = mxtl::AdoptRef(new (&ac) ChannelDispatcher(flags));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    auto ch1 = mxtl::AdoptRef(new (&ac) ChannelDispatcher(flags));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    ch0->Init(ch1);
    ch1->Init(ch0);

    *rights = MX_DEFAULT_CHANNEL_RIGHTS;
    *dispatcher0 = mxtl::move(ch0);
    *dispatcher1 = mxtl::move(ch1);
    return MX_OK;
}

ChannelDispatcher::ChannelDispatcher(uint32_t flags)
    : state_tracker_(MX_CHANNEL_WRITABLE) {
    DEBUG_ASSERT(flags == 0);
}

// This is called before either ChannelDispatcher is accessible from threads other than the one
// initializing the channel, so it does not need locking.
void ChannelDispatcher::Init(mxtl::RefPtr<ChannelDispatcher> other) TA_NO_THREAD_SAFETY_ANALYSIS {
    other_ = mxtl::move(other);
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

// Thread safety analysis disabled as this accesses guarded member variables without holding
// |lock_| after it knows there are no other references from other threads. See comment on last
// statement.
void ChannelDispatcher::on_zero_handles() TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

    // Detach other endpoint
    mxtl::RefPtr<ChannelDispatcher> other;
    {
        AutoLock lock(&lock_);
        other = mxtl::move(other_);

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

status_t ChannelDispatcher::Read(uint32_t* msg_size,
                                 uint32_t* msg_handle_count,
                                 mxtl::unique_ptr<MessagePacket>* msg,
                                 bool may_discard) {
    canary_.Assert();

    auto max_size = *msg_size;
    auto max_handle_count = *msg_handle_count;

    AutoLock lock(&lock_);

    if (messages_.is_empty())
        return other_ ? MX_ERR_SHOULD_WAIT : MX_ERR_PEER_CLOSED;

    *msg_size = messages_.front().data_size();
    *msg_handle_count = messages_.front().num_handles();
    status_t rv = MX_OK;
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

status_t ChannelDispatcher::Write(mxtl::unique_ptr<MessagePacket> msg) {
    canary_.Assert();

    mxtl::RefPtr<ChannelDispatcher> other;
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

    if (other->WriteSelf(mxtl::move(msg)) > 0)
        thread_reschedule();

    return MX_OK;
}

status_t ChannelDispatcher::Call(mxtl::unique_ptr<MessagePacket> msg,
                                 mx_time_t deadline, bool* return_handles,
                                 mxtl::unique_ptr<MessagePacket>* reply) {

    canary_.Assert();

    auto waiter = UserThread::GetCurrent()->GetMessageWaiter();
    if (unlikely(waiter->BeginWait(mxtl::WrapRefPtr(this), msg->get_txid()) != MX_OK)) {
        // If a thread tries BeginWait'ing twice, the VDSO contract around retrying
        // channel calls has been violated.  Shoot the misbehaving process.
        ProcessDispatcher::GetCurrent()->Kill();
        return MX_ERR_BAD_STATE;
    }

    mxtl::RefPtr<ChannelDispatcher> other;
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
    other->WriteSelf(mxtl::move(msg));

    // Reuse the code from the half-call used for retrying a Call after thread
    // suspend.
    return ResumeInterruptedCall(deadline, reply);
}

status_t ChannelDispatcher::ResumeInterruptedCall(mx_time_t deadline,
                                                  mxtl::unique_ptr<MessagePacket>* reply) {
    canary_.Assert();

    auto waiter = UserThread::GetCurrent()->GetMessageWaiter();

    // (2) Wait for notification via waiter's event or for the
    // deadline to hit.
    mx_status_t status = waiter->Wait(deadline);
    if (status == MX_ERR_INTERRUPTED_RETRY) {
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
        // and get_msg() returns a non-TIMED_OUT status.
        // Otherwise, the status is MX_ERR_TIMED_OUT and it
        // is our job to remove the waiter from the list.
        if ((status = waiter->EndWait(reply)) == MX_ERR_TIMED_OUT)
            waiters_.erase(*waiter);
    }

    return status;
}

int ChannelDispatcher::WriteSelf(mxtl::unique_ptr<MessagePacket> msg) {
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
                return waiter.Deliver(mxtl::move(msg));
            }
        }
    }
    messages_.push_back(mxtl::move(msg));

    state_tracker_.UpdateState(0u, MX_CHANNEL_READABLE);
    return 0;
}

status_t ChannelDispatcher::user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) {
    canary_.Assert();

    if ((set_mask & ~MX_USER_SIGNAL_ALL) || (clear_mask & ~MX_USER_SIGNAL_ALL))
        return MX_ERR_INVALID_ARGS;

    if (!peer) {
        state_tracker_.UpdateState(clear_mask, set_mask);
        return MX_OK;
    }

    mxtl::RefPtr<ChannelDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_)
            return MX_ERR_PEER_CLOSED;
        other = other_;
    }

    return other->UserSignalSelf(clear_mask, set_mask);
}

status_t ChannelDispatcher::UserSignalSelf(uint32_t clear_mask, uint32_t set_mask) {
    canary_.Assert();
    state_tracker_.UpdateState(clear_mask, set_mask);
    return MX_OK;
}


int ChannelDispatcher::MessageWaiter::Deliver(mxtl::unique_ptr<MessagePacket> msg) {
    DEBUG_ASSERT(armed());

    txid_ = msg->get_txid();
    msg_ = mxtl::move(msg);
    status_ = MX_OK;
    return event_.Signal(MX_OK);
}
