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

#include <zircon/types.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/unique_ptr.h>

class ChannelDispatcher final : public PeeredDispatcher<ChannelDispatcher> {
public:
    class MessageWaiter;

    static zx_status_t Create(fbl::RefPtr<Dispatcher>* dispatcher0,
                              fbl::RefPtr<Dispatcher>* dispatcher1, zx_rights_t* rights);

    ~ChannelDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_CHANNEL; }
    bool has_state_tracker() const final { return true; }
    zx_status_t add_observer(StateObserver* observer) final;

    // Read from this endpoint's message queue.
    // |msg_size| and |msg_handle_count| are in-out parameters. As input, they specify the maximum
    // size and handle count, respectively. On ZX_OK or ZX_ERR_BUFFER_TOO_SMALL, they specify the
    // actual size and handle count of the next message. The next message is returned in |*msg| on
    // ZX_OK and also on ZX_ERR_BUFFER_TOO_SMALL when |may_discard| is set.
    zx_status_t Read(uint32_t* msg_size,
                     uint32_t* msg_handle_count,
                     fbl::unique_ptr<MessagePacket>* msg,
                     bool may_disard);

    // Write to the opposing endpoint's message queue.
    zx_status_t Write(fbl::unique_ptr<MessagePacket> msg) TA_NO_THREAD_SAFETY_ANALYSIS;
    zx_status_t Call(fbl::unique_ptr<MessagePacket> msg, zx_time_t deadline,
                     fbl::unique_ptr<MessagePacket>* reply) TA_NO_THREAD_SAFETY_ANALYSIS;

    // Performs the wait-then-read half of Call.  This is meant for retrying
    // after an interruption caused by suspending.
    zx_status_t ResumeInterruptedCall(MessageWaiter* waiter, zx_time_t deadline,
                                      fbl::unique_ptr<MessagePacket>* reply);

    // Returns the maximum depth this channel endpoint will queue
    // messages to. This value is accessible to userspace via the
    // ZX_PROP_CHANNEL_TX_MSG_MAX object property.
    size_t TxMessageMax() const;

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
        MessageWaiter() : txid_(0), status_(ZX_ERR_BAD_STATE) {
        }

        ~MessageWaiter();

        zx_status_t BeginWait(fbl::RefPtr<ChannelDispatcher> channel);
        void Deliver(fbl::unique_ptr<MessagePacket> msg);
        void Cancel(zx_status_t status);
        fbl::RefPtr<ChannelDispatcher> get_channel() { return channel_; }
        zx_txid_t get_txid() const { return txid_; }
        void set_txid(zx_txid_t txid) { txid_ = txid; };
        zx_status_t Wait(zx_time_t deadline);
        // Returns any delivered message via out and the status.
        zx_status_t EndWait(fbl::unique_ptr<MessagePacket>* out);

    private:
        fbl::RefPtr<ChannelDispatcher> channel_;
        fbl::unique_ptr<MessagePacket> msg_;
        // TODO(teisenbe/swetland): Investigate hoisting this outside to reduce
        // userthread size
        Event event_;
        zx_txid_t txid_;
        zx_status_t status_;
    };

    // PeeredDispatcher implementation.
    void on_zero_handles_locked() TA_REQ(get_lock());
    void OnPeerZeroHandlesLocked() TA_REQ(get_lock());

private:
    using MessageList = fbl::DoublyLinkedList<fbl::unique_ptr<MessagePacket>>;
    using WaiterList = fbl::DoublyLinkedList<MessageWaiter*>;

    void RemoveWaiter(MessageWaiter* waiter);

    explicit ChannelDispatcher(fbl::RefPtr<PeerHolder<ChannelDispatcher>> holder);
    void Init(fbl::RefPtr<ChannelDispatcher> other);
    void WriteSelf(fbl::unique_ptr<MessagePacket> msg) TA_REQ(get_lock());
    zx_status_t UserSignalSelf(uint32_t clear_mask, uint32_t set_mask) TA_REQ(get_lock());

    fbl::Canary<fbl::magic("CHAN")> canary_;

    MessageList messages_ TA_GUARDED(get_lock());
    uint64_t message_count_ TA_GUARDED(get_lock()) = 0;
    uint64_t max_message_count_ TA_GUARDED(get_lock()) = 0;
    uint32_t txid_ TA_GUARDED(get_lock()) = 0;
    WaiterList waiters_ TA_GUARDED(get_lock());
};
