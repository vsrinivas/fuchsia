// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <memory>
#include <unordered_map>

#include <async/task.h>

#include "garnet/drivers/bluetooth/lib/att/att.h"
#include "garnet/drivers/bluetooth/lib/att/packet.h"
#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/linked_list.h"
#include "garnet/drivers/bluetooth/lib/common/optional.h"
#include "garnet/drivers/bluetooth/lib/common/packet_view.h"
#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"
#include "garnet/drivers/bluetooth/lib/l2cap/sdu.h"

#include "lib/fxl/functional/cancelable_callback.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/synchronization/thread_checker.h"
#include "lib/fxl/tasks/task_runner.h"

namespace btlib {
namespace att {

// Implements an ATT data bearer with the following features:
//
//   * This can be used over either a LE-U or an ACL-U logical link. No
//     assumptions are made on the logical transport of the underlying L2CAP
//     channel.
//   * Can simultaneously operate in both server and client roles of the
//     protocol.
//
// Deleting a Bearer closes the underlying channel. Unlike ShutDown(), this
// does NOT notify any callbacks to prevent them from running in destructors.
//
// THREAD-SAFETY:
//
// This class is intended to be created, accessed, and destroyed on the same
// thread. All callbacks will be invoked on a Bearer's creation thread.
class Bearer final : public fxl::RefCountedThreadSafe<Bearer> {
 public:
  inline static fxl::RefPtr<Bearer> Create(
      std::unique_ptr<l2cap::Channel>&& chan,
      uint32_t transaction_timeout_ms = kDefaultTransactionTimeoutMs) {
    return fxl::AdoptRef(new Bearer(std::move(chan), transaction_timeout_ms));
  }

  // Returns true if the underlying channel is open.
  bool is_open() const { return static_cast<bool>(chan_); }

  // The bi-directional (client + server) MTU currently in use. The default
  // value is kLEMinMTU.
  //
  // NOTE: This is allowed to be initialized to something smaller than kLEMinMTU
  // for unit tests.
  uint16_t mtu() const { return mtu_; }
  void set_mtu(uint16_t value) {
    FXL_VLOG(1) << "att: Bearer: new MTU: " << value;
    mtu_ = value;
  }

  // The preferred MTU. This is initially assigned based on the MTU of the
  // underlying L2CAP channel and will be used in future MTU Exchange
  // procedures.
  uint16_t preferred_mtu() const { return preferred_mtu_; }
  void set_preferred_mtu(uint16_t value) {
    FXL_DCHECK(value >= kLEMinMTU);
    preferred_mtu_ = value;
  }

  // Returns the correct minimum ATT_MTU based on the underlying link type.
  uint16_t min_mtu() const { return min_mtu_; }

  // Sets a callback to be invoked invoked when the underlying channel has
  // closed. |callback| should disconnect the underlying logical link.
  void set_closed_callback(const fxl::Closure& callback) {
    FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
    closed_cb_ = callback;
  }

  // Closes the channel. This should be called when a protocol transaction
  // warrants the link to be disconnected. Notifies any callback set via
  // |set_closed_callback()| and notifies the error callback of all pending HCI
  // transactions.
  //
  // Does nothing if the channel is not open.
  //
  // NOTE: Bearer internally shuts down the link on request timeouts and
  // sequential protocol violations.
  void ShutDown();

  // Initiates an asynchronous transaction and invokes |callback| on this
  // Bearer's creation thread when the transaction completes. |pdu| must
  // correspond to a request or indication.
  //
  // TransactionCallback is used to report the end of a request or indication
  // transaction. |packet| will contain a matching response or confirmation PDU
  // depending on the transaction in question.
  //
  // If the transaction ends with an error or cannot complete (e.g. due to a
  // timeout), |error_callback| will be called instead of |callback| if
  // provided.
  //
  // Returns false if |pdu| is malformed or does not correspond to a request or
  // indication.
  using TransactionCallback = std::function<void(const PacketReader& packet)>;
  using ErrorCallback = std::function<
      void(bool timeout, ErrorCode protocol_error, Handle attr_in_error)>;
  bool StartTransaction(common::ByteBufferPtr pdu,
                        const TransactionCallback& callback,
                        const ErrorCallback& error_callback);

  // Sends |pdu| without initiating a transaction. Used for command and
  // notification PDUs which do not have flow control.
  //
  // Returns false if the packet is malformed or does not correspond to a
  // command or notification.
  bool SendWithoutResponse(common::ByteBufferPtr pdu);

  // A Handler is a function that gets invoked when the Bearer receives a PDU
  // that is not tied to a locally initiated transaction (see
  // StartTransaction()).
  //
  //   * |tid| will be set to kInvalidTransactionId for command and notification
  //     PDUs. These do not require a response and are not part of a
  //     transaction.
  //
  //   * A valid |tid| will be provided for request and indication PDUs. These
  //     require a response which can be sent by calling Reply().
  using TransactionId = size_t;
  static constexpr TransactionId kInvalidTransactionId = 0u;
  using Handler =
      std::function<void(TransactionId tid, const PacketReader& packet)>;

  // Handler: called when |pdu| does not need flow control. This will be
  // called for commands and notifications.
  using HandlerId = size_t;
  static constexpr HandlerId kInvalidHandlerId = 0u;
  HandlerId RegisterHandler(OpCode opcode, const Handler& handler);

  // Unregisters a handler. |id| cannot be zero.
  void UnregisterHandler(HandlerId id);

  // Ends a currently pending transaction with the given response or
  // confirmation |pdu|. Returns false if |pdu| is malformed or if |id| and
  // |pdu| do not match a pending transaction.
  bool Reply(TransactionId tid, common::ByteBufferPtr pdu);

  // Ends a request transaction with an error response.
  bool ReplyWithError(TransactionId id, Handle handle, ErrorCode error_code);

  // Sets the transaction timeout interval. This is intended for unit tests.
  void set_transaction_timeout_ms(uint32_t value) {
    FXL_DCHECK(value);
    transaction_timeout_ms_ = value;
  }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(Bearer);

  Bearer(std::unique_ptr<l2cap::Channel> chan, uint32_t transaction_timeout_ms);
  ~Bearer();

  // Represents a locally initiated pending request or indication transaction.
  struct PendingTransaction : common::LinkedListable<PendingTransaction> {
    PendingTransaction(OpCode opcode,
                       TransactionCallback callback,
                       ErrorCallback error_callback,
                       common::ByteBufferPtr pdu);

    // Required fields
    OpCode opcode;
    TransactionCallback callback;

    // Optional error callback
    ErrorCallback error_callback;

    // Holds the pdu while the transaction is in the send queue.
    common::ByteBufferPtr pdu;
  };
  using PendingTransactionPtr = std::unique_ptr<PendingTransaction>;

  // Represents a remote initiated pending request or indication transaction.
  struct PendingRemoteTransaction {
    PendingRemoteTransaction(TransactionId id, OpCode opcode);
    PendingRemoteTransaction() = default;

    TransactionId id;
    OpCode opcode;
  };

  // Used the represent the state of active ATT protocol request and indication
  // transactions.
  class TransactionQueue {
   public:
    TransactionQueue() = default;
    ~TransactionQueue();

    // Returns the transaction that has been sent to the peer and is currently
    // pending completion.
    inline PendingTransaction* current() const { return current_.get(); }

    // Clears the currently pending transaction data and cancels its transaction
    // timeout task. Returns ownership of any pending transaction to the caller.
    PendingTransactionPtr ClearCurrent();

    // Tries to initiate the next transaction. Sends the PDU over |chan| if
    // successful.
    void TrySendNext(l2cap::Channel* chan,
                     const fxl::Closure& timeout_cb,
                     uint32_t timeout_ms);

    // Adds |next| to the transaction queue.
    void Enqueue(PendingTransactionPtr transaction);

    // Resets the contents of this queue to their default state.
    void Reset();

    // Invokes the error callbacks of all transactions.
    void InvokeErrorAll(bool timeout, ErrorCode error_code);

   private:
    void SetTimeout(const fxl::Closure& callback, uint32_t timeout_ms);
    void CancelTimeout();

    common::LinkedList<PendingTransaction> queue_;
    PendingTransactionPtr current_;
    std::unique_ptr<async::Task> timeout_task_;
  };

  bool SendInternal(common::ByteBufferPtr pdu,
                    const TransactionCallback& callback,
                    const ErrorCallback& error_callback);

  // Shuts down the link.
  void ShutDownInternal(bool due_to_timeout);

  // Returns false if |pdu| is malformed.
  bool IsPacketValid(const common::ByteBuffer& pdu);

  // Tries to initiate the next transaction from the given |queue|.
  void TryStartNextTransaction(TransactionQueue* tq);

  // Sends out an immediate error response.
  void SendErrorResponse(OpCode request_opcode,
                         Handle attribute_handle,
                         ErrorCode error_code);

  // Called when the peer sends us a response or confirmation PDU.
  void HandleEndTransaction(TransactionQueue* tq, const PacketReader& packet);

  // Returns the next handler ID and increments the counter.
  HandlerId NextHandlerId();

  // Returns the next remote-initiated transaction ID.
  TransactionId NextRemoteTransactionId();

  using RemoteTransaction = common::Optional<PendingRemoteTransaction>;

  // Called when the peer initiates a request or indication transaction.
  void HandleBeginTransaction(RemoteTransaction* currently_pending,
                              const PacketReader& packet);

  // Returns any pending peer-initiated transaction that matches |id|. Returns
  // nullptr otherwise.
  RemoteTransaction* FindRemoteTransaction(TransactionId id);

  // Called when the peer sends us a PDU that has no flow control (i.e. command
  // or notification). Routes the packet to the corresponding handler. Drops the
  // packet if no handler exists.
  void HandlePDUWithoutResponse(const PacketReader& packet);

  // l2cap::Channel callbacks:
  void OnChannelClosed();
  void OnRxBFrame(const l2cap::SDU& sdu);

  std::unique_ptr<l2cap::Channel> chan_;
  uint16_t mtu_;
  uint16_t preferred_mtu_;
  uint16_t min_mtu_;

  // Callback passed to l2cap::Channel::OnRxBFrame().
  fxl::CancelableCallback<void(const l2cap::SDU& sdu)> rx_task_;

  // Channel closed callback.
  fxl::Closure closed_cb_;

  // The timeout interval (in milliseconds) used for local initiated ATT
  // transactions.
  uint32_t transaction_timeout_ms_;

  // The state of outgoing ATT requests and indications
  TransactionQueue request_queue_;
  TransactionQueue indication_queue_;

  // The transaction identifier that will be assigned to the next
  // remote-initiated request or indication transaction.
  TransactionId next_remote_transaction_id_;

  // The next available remote-initiated PDU handler id.
  HandlerId next_handler_id_;

  // Data about currently registered handlers.
  std::unordered_map<HandlerId, OpCode> handler_id_map_;
  std::unordered_map<OpCode, Handler> handlers_;

  // Remote-initiated transactions in progress.
  RemoteTransaction remote_request_;
  RemoteTransaction remote_indication_;

  fxl::ThreadChecker thread_checker_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Bearer);
};

}  // namespace att
}  // namespace btlib
