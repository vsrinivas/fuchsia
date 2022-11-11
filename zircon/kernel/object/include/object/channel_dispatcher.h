// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_CHANNEL_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_CHANNEL_DISPATCHER_H_

#include <stdint.h>
#include <zircon/rights.h>
#include <zircon/types.h>

#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <kernel/event.h>
#include <kernel/mutex.h>
#include <ktl/unique_ptr.h>
#include <object/dispatcher.h>
#include <object/handle.h>
#include <object/message_packet.h>

class ChannelDispatcher final
    : public PeeredDispatcher<ChannelDispatcher, ZX_DEFAULT_CHANNEL_RIGHTS> {
 public:
  class MessageWaiter;

  static zx_status_t Create(KernelHandle<ChannelDispatcher>* handle0,
                            KernelHandle<ChannelDispatcher>* handle1, zx_rights_t* rights);

  ~ChannelDispatcher() final;
  zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_CHANNEL; }

  // Read from this endpoint's message queue.
  // |owner| is the handle table koid of the process attempting to read from the channel.
  // |msg_size| and |msg_handle_count| are in-out parameters. As input, they specify the maximum
  // size and handle count, respectively. On ZX_OK or ZX_ERR_BUFFER_TOO_SMALL, they specify the
  // actual size and handle count of the next message. The next message is returned in |*msg| on
  // ZX_OK and also on ZX_ERR_BUFFER_TOO_SMALL when |may_discard| is set.
  zx_status_t Read(zx_koid_t owner, uint32_t* msg_size, uint32_t* msg_handle_count,
                   MessagePacketPtr* msg, bool may_disard);

  // Write to the opposing endpoint's message queue. |owner| is the handle table koid of the process
  // attempting to write to the channel, or ZX_KOID_INVALID if kernel is doing it.
  zx_status_t Write(zx_koid_t owner, MessagePacketPtr msg);

  // Perform a transacted Write + Read. |owner| is the handle table koid of the process attempting
  // to write to the channel, or ZX_KOID_INVALID if kernel is doing it.
  zx_status_t Call(zx_koid_t owner, MessagePacketPtr msg, zx_time_t deadline,
                   MessagePacketPtr* reply);

  // Performs the wait-then-read half of Call.  This is meant for retrying
  // after an interruption caused by suspending.
  zx_status_t ResumeInterruptedCall(MessageWaiter* waiter, const Deadline& deadline,
                                    MessagePacketPtr* reply);

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
    MessageWaiter() : txid_(0), status_(ZX_ERR_BAD_STATE) {}

    ~MessageWaiter();

    zx_status_t BeginWait(fbl::RefPtr<ChannelDispatcher> channel);
    void Deliver(MessagePacketPtr msg);
    void Cancel(zx_status_t status);
    fbl::RefPtr<ChannelDispatcher> get_channel() { return channel_; }
    zx_txid_t get_txid() const { return txid_; }
    void set_txid(zx_txid_t txid) { txid_ = txid; }
    zx_status_t Wait(const Deadline& deadline);
    // Returns any delivered message via out and the status.
    zx_status_t EndWait(MessagePacketPtr* out);

   private:
    fbl::RefPtr<ChannelDispatcher> channel_;
    MessagePacketPtr msg_;
    // TODO(teisenbe/swetland): Investigate hoisting this outside to reduce
    // userthread size
    Event event_;
    zx_txid_t txid_;
    zx_status_t status_;
  };

  struct MessageCounts {
    uint64_t current{};
    uint64_t max{};
  };
  MessageCounts get_message_counts() const TA_EXCL(&channel_lock_) {
    Guard<CriticalMutex> guard{&channel_lock_};
    return {messages_.size(), max_message_count_};
  }

  // Returns the number of times a channel reached the max pending message count,
  // |kMaxPendingMessageCount|.
  static int64_t get_channel_full_count();

  // PeeredDispatcher implementation.
  void on_zero_handles_locked() TA_REQ(get_lock());
  void OnPeerZeroHandlesLocked() TA_REQ(get_lock());

  void set_owner(zx_koid_t new_owner) final;

 private:
  using MessageList = fbl::SizedDoublyLinkedList<MessagePacketPtr>;
  using WaiterList = fbl::DoublyLinkedList<MessageWaiter*>;

  explicit ChannelDispatcher(fbl::RefPtr<PeerHolder<ChannelDispatcher>> holder);

  void RemoveWaiter(MessageWaiter* waiter);

  // Attempt to deliver the message to a waiting MessageWaiter.
  //
  // Returns true and takes ownership of |msg| iff the message was delivered.
  bool TryWriteToMessageWaiter(MessagePacketPtr& msg) TA_REQ(get_lock());

  void WriteSelf(MessagePacketPtr msg) TA_REQ(get_lock());

  // Generate a unique txid to be used in a channel call.
  zx_txid_t GenerateTxid() TA_REQ(get_lock());

  // By using a dedicated lock to protect the fields accessed by Read, we can
  // avoid acquiring |get_lock()| in the Read path.  Why do we care?
  // |get_lock()| is held when raising signals and notifying matching observers.
  // And there can be a lot of matching observers.  By use two locks and never
  // acquiring |get_lock()| in the Read path we can allow Read to execute
  // concurrently with the observer notification.
  //
  // When acquiring both |get_lock()| and |channel_lock_| be sure to acquire
  // |get_lock()| first.
  mutable DECLARE_CRITICAL_MUTEX(ChannelDispatcher) channel_lock_ TA_ACQ_AFTER(get_lock());
  MessageList messages_ TA_GUARDED(channel_lock_);
  uint64_t max_message_count_ TA_GUARDED(channel_lock_) = 0;

  // Tracks the process that is allowed to issue calls, for example write
  // to the opposite end. Without it, one can see writes out of order with
  // respect of the previous and current owner. We avoid locking and updating
  // the |owner_| if the new owner is kernel, which happens when the endpoint
  // is written into a channel or during process destruction.
  //
  // The locking protocol for this field is a little tricky.  The Read method,
  // which only ever acquires the channel_lock_, must read this field.  The
  // Write method also needs to read this field, however, it needs to do so
  // before it would otherwise need to acquire the channel_lock_.  So to avoid
  // having Write prematurely acquire and release the channel_lock_, we instead
  // require that either |get_lock()| or channel_lock_ are held when reading
  // this field and both are held when writing it.
  zx_koid_t owner_ = ZX_KOID_INVALID;
  WaiterList waiters_ TA_GUARDED(get_lock());
  uint32_t txid_ TA_GUARDED(get_lock()) = 0;
  // True if the this object's peer has been closed.  This field exists so that
  // |Read| can check for peer closed without having to acquire |get_lock()|.
  bool peer_has_closed_ TA_GUARDED(channel_lock_) = false;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_CHANNEL_DISPATCHER_H_
