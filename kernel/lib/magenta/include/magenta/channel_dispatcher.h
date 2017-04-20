// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <magenta/dispatcher.h>
#include <magenta/message_packet.h>
#include <magenta/state_tracker.h>
#include <magenta/types.h>
#include <magenta/wait_event.h>

#include <mxtl/canary.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/ref_counted.h>
#include <mxtl/unique_ptr.h>

class ChannelDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t flags, mxtl::RefPtr<Dispatcher>* dispatcher0,
                           mxtl::RefPtr<Dispatcher>* dispatcher1, mx_rights_t* rights);

    ~ChannelDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_CHANNEL; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }
    mx_status_t add_observer(StateObserver* observer) final;
    mx_koid_t get_related_koid() const final TA_REQ(lock_) { return other_koid_; }
    status_t user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) final;

    void on_zero_handles() final;

    // Read from this endpoint's message queue.
    // |msg_size| and |msg_handle_count| are in-out parameters. As input, they specify the maximum
    // size and handle count, respectively. On MX_OK or MX_ERR_BUFFER_TOO_SMALL, they specify the
    // actual size and handle count of the next message. The next message is returned in |*msg| on
    // MX_OK and also on MX_ERR_BUFFER_TOO_SMALL when |may_discard| is set.
    status_t Read(uint32_t* msg_size,
                  uint32_t* msg_handle_count,
                  mxtl::unique_ptr<MessagePacket>* msg,
                  bool may_disard);

    // Write to the opposing endpoint's message queue.
    status_t Write(mxtl::unique_ptr<MessagePacket> msg);
    status_t Call(mxtl::unique_ptr<MessagePacket> msg,
                  mx_time_t deadline, bool* return_handles,
                  mxtl::unique_ptr<MessagePacket>* reply);

    // Performs the wait-then-read half of Call.  This is meant for retrying
    // after an interruption caused by suspending.
    status_t ResumeInterruptedCall(mx_time_t deadline, mxtl::unique_ptr<MessagePacket>* reply);

    // MessageWaiter's state is guarded by the lock of the
    // owning ChannelDispatcher, and Deliver(), Signal(), Cancel(),
    // and EndWait() methods must only be called under
    // that lock.
    //
    // MessageWaiters are embedded in UserThreads, and the channel_ pointer
    // can only be manipulated by their thread (via BeginWait() or EndWait()), and
    // only transitions to nullptr while holding the ChannelDispatcher's lock.
    //
    // See also: comments in ChannelDispatcher::Call()
    class MessageWaiter : public mxtl::DoublyLinkedListable<MessageWaiter*> {
    public:
        MessageWaiter() : txid_(0), status_(MX_ERR_BAD_STATE) {
        }

        ~MessageWaiter() {
            if (unlikely(channel_ != nullptr)) {
                channel_->RemoveWaiter(this);
            }
            DEBUG_ASSERT(!InContainer());
        }

        mx_status_t BeginWait(mxtl::RefPtr<ChannelDispatcher> channel, mx_txid_t txid) {
            if (unlikely(channel_ != nullptr)) {
                return MX_ERR_BAD_STATE;
            }
            DEBUG_ASSERT(!InContainer());

            txid_ = txid;
            status_ = MX_ERR_TIMED_OUT;
            channel_ = mxtl::move(channel);
            event_.Unsignal();
            return MX_OK;
        }

        int Deliver(mxtl::unique_ptr<MessagePacket> msg);

        int Cancel(status_t status) {
            DEBUG_ASSERT(!InContainer());
            DEBUG_ASSERT(armed());
            status_ = status;
            return event_.Signal(status);
        }

        mx_txid_t get_txid() const { return txid_; }

        mx_status_t Wait(lk_time_t deadline) {
            if (unlikely(!armed())) {
                return MX_ERR_BAD_STATE;
            }
            return event_.Wait(deadline);
        }

        // Returns any delivered message via out and the status.
        mx_status_t EndWait(mxtl::unique_ptr<MessagePacket>* out) {
            if (unlikely(!armed())) {
                return MX_ERR_BAD_STATE;
            }
            *out = mxtl::move(msg_);
            channel_ = nullptr;
            return status_;
        }

    private:
        bool armed() const { return channel_ != nullptr; }

        mxtl::RefPtr<ChannelDispatcher> channel_;
        mxtl::unique_ptr<MessagePacket> msg_;
        // TODO(teisenbe/swetland): Investigate hoisting this outside to reduce
        // userthread size
        WaitEvent event_;
        mx_txid_t txid_;
        mx_status_t status_;
    };

private:
    using MessageList = mxtl::DoublyLinkedList<mxtl::unique_ptr<MessagePacket>>;
    using WaiterList = mxtl::DoublyLinkedList<MessageWaiter*>;

    void RemoveWaiter(MessageWaiter* waiter);

    ChannelDispatcher(uint32_t flags);
    void Init(mxtl::RefPtr<ChannelDispatcher> other);
    int WriteSelf(mxtl::unique_ptr<MessagePacket> msg);
    status_t UserSignalSelf(uint32_t clear_mask, uint32_t set_mask);
    void OnPeerZeroHandles();

    mxtl::Canary<mxtl::magic("CHAN")> canary_;

    Mutex lock_;
    MessageList messages_ TA_GUARDED(lock_);
    WaiterList waiters_ TA_GUARDED(lock_);
    StateTracker state_tracker_;
    mxtl::RefPtr<ChannelDispatcher> other_ TA_GUARDED(lock_);
    mx_koid_t other_koid_ TA_GUARDED(lock_);
};
