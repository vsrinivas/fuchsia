// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/event.h>
#include <object/dispatcher.h>
#include <object/message_packet.h>
#include <object/state_tracker.h>

#include <magenta/types.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/unique_ptr.h>

class ChannelDispatcher final : public Dispatcher {
public:
    class MessageWaiter;

    static mx_status_t Create(fbl::RefPtr<Dispatcher>* dispatcher0,
                              fbl::RefPtr<Dispatcher>* dispatcher1, mx_rights_t* rights);

    ~ChannelDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_CHANNEL; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }
    mx_status_t add_observer(StateObserver* observer) final;
    mx_koid_t get_related_koid() const final TA_REQ(lock_) { return other_koid_; }
    mx_status_t user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) final;

    void on_zero_handles() final;

    // Read from this endpoint's message queue.
    // |msg_size| and |msg_handle_count| are in-out parameters. As input, they specify the maximum
    // size and handle count, respectively. On MX_OK or MX_ERR_BUFFER_TOO_SMALL, they specify the
    // actual size and handle count of the next message. The next message is returned in |*msg| on
    // MX_OK and also on MX_ERR_BUFFER_TOO_SMALL when |may_discard| is set.
    mx_status_t Read(uint32_t* msg_size,
                     uint32_t* msg_handle_count,
                     fbl::unique_ptr<MessagePacket>* msg,
                     bool may_disard);

    // Write to the opposing endpoint's message queue.
    mx_status_t Write(fbl::unique_ptr<MessagePacket> msg);
    mx_status_t Call(fbl::unique_ptr<MessagePacket> msg,
                     mx_time_t deadline, bool* return_handles,
                     fbl::unique_ptr<MessagePacket>* reply);

    // Performs the wait-then-read half of Call.  This is meant for retrying
    // after an interruption caused by suspending.
    mx_status_t ResumeInterruptedCall(MessageWaiter* waiter, mx_time_t deadline,
                                      fbl::unique_ptr<MessagePacket>* reply);

    // MessageWaiter's state is guarded by the lock of the
    // owning ChannelDispatcher, and Deliver(), Signal(), Cancel(),
    // and EndWait() methods must only be called under
    // that lock.
    //
    // MessageWaiters are embedded in ThreadDispatchers, and the channel_ pointer
    // can only be manipulated by their thread (via BeginWait() or EndWait()), and
    // only transitions to nullptr while holding the ChannelDispatcher's lock.
    //
    // See also: comments in ChannelDispatcher::Call()
    class MessageWaiter : public fbl::DoublyLinkedListable<MessageWaiter*> {
    public:
        MessageWaiter() : txid_(0), status_(MX_ERR_BAD_STATE) {
        }

        ~MessageWaiter();

        mx_status_t BeginWait(fbl::RefPtr<ChannelDispatcher> channel, mx_txid_t txid);
        int Deliver(fbl::unique_ptr<MessagePacket> msg);
        int Cancel(mx_status_t status);
        fbl::RefPtr<ChannelDispatcher> get_channel() { return channel_; }
        mx_txid_t get_txid() const { return txid_; }
        mx_status_t Wait(lk_time_t deadline);
        // Returns any delivered message via out and the status.
        mx_status_t EndWait(fbl::unique_ptr<MessagePacket>* out);

    private:
        fbl::RefPtr<ChannelDispatcher> channel_;
        fbl::unique_ptr<MessagePacket> msg_;
        // TODO(teisenbe/swetland): Investigate hoisting this outside to reduce
        // userthread size
        Event event_;
        mx_txid_t txid_;
        mx_status_t status_;
    };

private:
    using MessageList = fbl::DoublyLinkedList<fbl::unique_ptr<MessagePacket>>;
    using WaiterList = fbl::DoublyLinkedList<MessageWaiter*>;

    void RemoveWaiter(MessageWaiter* waiter);

    ChannelDispatcher();
    void Init(fbl::RefPtr<ChannelDispatcher> other);
    int WriteSelf(fbl::unique_ptr<MessagePacket> msg);
    mx_status_t UserSignalSelf(uint32_t clear_mask, uint32_t set_mask);
    void OnPeerZeroHandles();

    fbl::Canary<fbl::magic("CHAN")> canary_;

    fbl::Mutex lock_;
    MessageList messages_ TA_GUARDED(lock_);
    WaiterList waiters_ TA_GUARDED(lock_);
    StateTracker state_tracker_;
    fbl::RefPtr<ChannelDispatcher> other_ TA_GUARDED(lock_);
    mx_koid_t other_koid_ TA_GUARDED(lock_);
};
