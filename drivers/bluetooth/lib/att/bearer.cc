// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bearer.h"

#include <lib/async/default.h>

#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"
#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"

#include "lib/fxl/strings/string_printf.h"

namespace btlib {
namespace att {

// static
constexpr Bearer::HandlerId Bearer::kInvalidHandlerId;
constexpr Bearer::TransactionId Bearer::kInvalidTransactionId;

namespace {

MethodType GetMethodType(OpCode opcode) {
  // We treat all packets as a command if the command bit was set. An
  // unrecognized command will always be ignored (so it is OK to return kCommand
  // here if, for example, |opcode| is a response with the command-bit set).
  if (opcode & kCommandFlag)
    return MethodType::kCommand;

  switch (opcode) {
    case kInvalidOpCode:
      return MethodType::kInvalid;

    case kExchangeMTURequest:
    case kFindInformationRequest:
    case kFindByTypeValueRequest:
    case kReadByTypeRequest:
    case kReadRequest:
    case kReadBlobRequest:
    case kReadMultipleRequest:
    case kReadByGroupTypeRequest:
    case kWriteRequest:
    case kPrepareWriteRequest:
    case kExecuteWriteRequest:
      return MethodType::kRequest;

    case kErrorResponse:
    case kExchangeMTUResponse:
    case kFindInformationResponse:
    case kFindByTypeValueResponse:
    case kReadByTypeResponse:
    case kReadResponse:
    case kReadBlobResponse:
    case kReadMultipleResponse:
    case kReadByGroupTypeResponse:
    case kWriteResponse:
    case kPrepareWriteResponse:
    case kExecuteWriteResponse:
      return MethodType::kResponse;

    case kNotification:
      return MethodType::kNotification;
    case kIndication:
      return MethodType::kIndication;
    case kConfirmation:
      return MethodType::kConfirmation;

    // These are redundant with the check above but are included for
    // completeness.
    case kWriteCommand:
    case kSignedWriteCommand:
      return MethodType::kCommand;

    default:
      break;
  }

  // Everything else will be treated as an incoming request.
  return MethodType::kRequest;
}

// Returns the corresponding originating transaction opcode for
// |transaction_end_code|, where the latter must correspond to a response or
// confirmation.
OpCode MatchingTransactionCode(OpCode transaction_end_code) {
  switch (transaction_end_code) {
    case kExchangeMTUResponse:
      return kExchangeMTURequest;
    case kFindInformationResponse:
      return kFindInformationRequest;
    case kFindByTypeValueResponse:
      return kFindByTypeValueRequest;
    case kReadByTypeResponse:
      return kReadByTypeRequest;
    case kReadResponse:
      return kReadRequest;
    case kReadBlobResponse:
      return kReadBlobRequest;
    case kReadMultipleResponse:
      return kReadMultipleRequest;
    case kReadByGroupTypeResponse:
      return kReadByGroupTypeRequest;
    case kWriteResponse:
      return kWriteRequest;
    case kPrepareWriteResponse:
      return kPrepareWriteRequest;
    case kExecuteWriteResponse:
      return kExecuteWriteRequest;
    case kConfirmation:
      return kIndication;
    default:
      break;
  }

  return kInvalidOpCode;
}

}  // namespace

// static
fxl::RefPtr<Bearer> Bearer::Create(fbl::RefPtr<l2cap::Channel> chan) {
  auto bearer = fxl::AdoptRef(new Bearer(std::move(chan)));
  return bearer->Activate() ? bearer : nullptr;
}

Bearer::PendingTransaction::PendingTransaction(OpCode opcode,
                                               TransactionCallback callback,
                                               ErrorCallback error_callback,
                                               common::ByteBufferPtr pdu)
    : opcode(opcode),
      callback(std::move(callback)),
      error_callback(std::move(error_callback)),
      pdu(std::move(pdu)) {
  FXL_DCHECK(this->callback);
  FXL_DCHECK(this->error_callback);
  FXL_DCHECK(this->pdu);
}

Bearer::PendingRemoteTransaction::PendingRemoteTransaction(TransactionId id,
                                                           OpCode opcode)
    : id(id), opcode(opcode) {}

Bearer::TransactionQueue::TransactionQueue(TransactionQueue&& other)
    : queue_(std::move(other.queue_)), current_(std::move(other.current_)) {
  // The move constructor is only used during shut down below. So we simply
  // cancel the task and not worry about moving it.
  other.timeout_task_.Cancel();
}

Bearer::PendingTransactionPtr Bearer::TransactionQueue::ClearCurrent() {
  FXL_DCHECK(current_);
  FXL_DCHECK(timeout_task_.is_pending());

  timeout_task_.Cancel();

  return std::move(current_);
}

void Bearer::TransactionQueue::Enqueue(PendingTransactionPtr transaction) {
  queue_.push_back(std::move(transaction));
}

void Bearer::TransactionQueue::TrySendNext(l2cap::Channel* chan,
                                           async::Task::Handler timeout_cb,
                                           uint32_t timeout_ms) {
  FXL_DCHECK(chan);

  // Abort if a transaction is currently pending.
  if (current())
    return;

  // Advance to the next transaction.
  current_ = queue_.pop_front();
  if (current()) {
    FXL_DCHECK(!timeout_task_.is_pending());
    timeout_task_.set_handler(std::move(timeout_cb));
    timeout_task_.PostDelayed(async_get_default_dispatcher(), zx::msec(timeout_ms));
    chan->Send(std::move(current()->pdu));
  }
}

void Bearer::TransactionQueue::Reset() {
  timeout_task_.Cancel();
  queue_.clear();
  current_ = nullptr;
}

void Bearer::TransactionQueue::InvokeErrorAll(Status status) {
  if (current_) {
    current_->error_callback(status, kInvalidHandle);
  }

  for (const auto& t : queue_) {
    if (t.error_callback)
      t.error_callback(status, kInvalidHandle);
  }
}

Bearer::Bearer(fbl::RefPtr<l2cap::Channel> chan)
    : chan_(std::move(chan)),
      next_remote_transaction_id_(1u),
      next_handler_id_(1u) {
  FXL_DCHECK(chan_);

  if (chan_->link_type() == hci::Connection::LinkType::kLE) {
    min_mtu_ = kLEMinMTU;
  } else {
    min_mtu_ = kBREDRMinMTU;
  }

  mtu_ = min_mtu();
  preferred_mtu_ =
      std::max(min_mtu(), std::min(chan_->tx_mtu(), chan_->rx_mtu()));
}

Bearer::~Bearer() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  rx_task_.Cancel();
  chan_ = nullptr;

  request_queue_.Reset();
  indication_queue_.Reset();
}

bool Bearer::Activate() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  rx_task_.Reset(fit::bind_member(this, &Bearer::OnRxBFrame));
  chan_closed_cb_.Reset(fit::bind_member(this, &Bearer::OnChannelClosed));

  return chan_->Activate(rx_task_.callback(), chan_closed_cb_.callback(),
                         async_get_default_dispatcher());
}

void Bearer::ShutDown() {
  if (is_open())
    ShutDownInternal(false /* due_to_timeout */);
}

void Bearer::ShutDownInternal(bool due_to_timeout) {
  FXL_DCHECK(is_open());
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  FXL_VLOG(1) << "att: Bearer shutting down";

  rx_task_.Cancel();
  chan_closed_cb_.Cancel();

  // This will have no effect if the channel is already closed (e.g. if
  // ShutDown() was called by OnChannelClosed()).
  chan_->SignalLinkError();
  chan_ = nullptr;

  // Move the contents to temporaries. This prevents a potential memory
  // corruption in InvokeErrorAll if the Bearer gets deleted by one of the
  // invoked error callbacks.
  TransactionQueue req_queue(std::move(request_queue_));
  TransactionQueue ind_queue(std::move(indication_queue_));

  if (closed_cb_)
    closed_cb_();

  // Terminate all remaining procedures with an error. This is safe even if
  // the bearer got deleted by |closed_cb_|.
  Status status(due_to_timeout ? common::HostError::kTimedOut
                               : common::HostError::kFailed);
  req_queue.InvokeErrorAll(status);
  ind_queue.InvokeErrorAll(status);
}

bool Bearer::StartTransaction(common::ByteBufferPtr pdu,
                              TransactionCallback callback,
                              ErrorCallback error_callback) {
  FXL_DCHECK(pdu);
  FXL_DCHECK(callback);
  FXL_DCHECK(error_callback);

  return SendInternal(std::move(pdu), std::move(callback), std::move(error_callback));
}

bool Bearer::SendWithoutResponse(common::ByteBufferPtr pdu) {
  FXL_DCHECK(pdu);
  return SendInternal(std::move(pdu), {}, {});
}

bool Bearer::SendInternal(common::ByteBufferPtr pdu,
                          TransactionCallback callback,
                          ErrorCallback error_callback) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  if (!is_open()) {
    FXL_VLOG(2) << "att: Bearer closed";
    return false;
  }

  if (!IsPacketValid(*pdu)) {
    FXL_VLOG(1) << "att: Packet has bad length!";
    return false;
  }

  PacketReader reader(pdu.get());
  MethodType type = GetMethodType(reader.opcode());

  TransactionQueue* tq = nullptr;

  switch (type) {
    case MethodType::kCommand:
    case MethodType::kNotification:
      if (callback || error_callback) {
        FXL_VLOG(1) << "att: Method not a transaction";
        return false;
      }

      // Send the command. No flow control is necessary.
      chan_->Send(std::move(pdu));
      return true;

    case MethodType::kRequest:
      tq = &request_queue_;
      break;
    case MethodType::kIndication:
      tq = &indication_queue_;
      break;
    default:
      FXL_VLOG(1) << "att: invalid opcode: " << reader.opcode();
      return false;
  }

  if (!callback || !error_callback) {
    FXL_VLOG(1) << "att: Transaction requires callbacks";
    return false;
  }

  tq->Enqueue(std::make_unique<PendingTransaction>(
      reader.opcode(), std::move(callback), std::move(error_callback), std::move(pdu)));
  TryStartNextTransaction(tq);

  return true;
}

Bearer::HandlerId Bearer::RegisterHandler(OpCode opcode,
                                          Handler handler) {
  FXL_DCHECK(handler);

  if (!is_open())
    return kInvalidHandlerId;

  if (handlers_.find(opcode) != handlers_.end()) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "att: Can only register one Handler per opcode (0x%02x)", opcode);
    return kInvalidHandlerId;
  }

  HandlerId id = NextHandlerId();
  if (id == kInvalidHandlerId)
    return kInvalidHandlerId;

  auto res = handler_id_map_.emplace(id, opcode);
  FXL_CHECK(res.second) << "att: Handler ID got reused (id: " << id << ")";

  handlers_[opcode] = std::move(handler);

  return id;
}

void Bearer::UnregisterHandler(HandlerId id) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(id != kInvalidHandlerId);

  auto iter = handler_id_map_.find(id);
  if (iter == handler_id_map_.end()) {
    FXL_VLOG(1) << "att: Cannot unregister unknown handler id: " << id;
    return;
  }

  OpCode opcode = iter->second;
  handlers_.erase(opcode);
}

bool Bearer::Reply(TransactionId tid, common::ByteBufferPtr pdu) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(pdu);

  if (tid == kInvalidTransactionId)
    return false;

  if (!is_open()) {
    FXL_VLOG(2) << "att: Bearer closed";
    return false;
  }

  if (!IsPacketValid(*pdu)) {
    FXL_VLOG(1) << "att: Invalid response PDU";
    return false;
  }

  RemoteTransaction* pending = FindRemoteTransaction(tid);
  if (!pending)
    return false;

  PacketReader reader(pdu.get());

  // Use ReplyWithError() instead.
  if (reader.opcode() == kErrorResponse)
    return false;

  OpCode pending_opcode = (*pending)->opcode;
  if (pending_opcode != MatchingTransactionCode(reader.opcode())) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "att: opcode does not match pending (pending: 0x%02x, given: 0x%02x)",
        pending_opcode, reader.opcode());
    return false;
  }

  pending->Reset();
  chan_->Send(std::move(pdu));

  return true;
}

bool Bearer::ReplyWithError(TransactionId id,
                            Handle handle,
                            ErrorCode error_code) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  RemoteTransaction* pending = FindRemoteTransaction(id);
  if (!pending)
    return false;

  OpCode pending_opcode = (*pending)->opcode;
  if (pending_opcode == kIndication) {
    FXL_VLOG(1) << "att: Cannot respond to an indication with error!";
    return false;
  }

  pending->Reset();
  SendErrorResponse(pending_opcode, handle, error_code);

  return true;
}

bool Bearer::IsPacketValid(const common::ByteBuffer& packet) {
  return packet.size() != 0u && packet.size() <= mtu_;
}

void Bearer::TryStartNextTransaction(TransactionQueue* tq) {
  FXL_DCHECK(is_open());
  FXL_DCHECK(tq);

  tq->TrySendNext(chan_.get(),
                  [this](async_dispatcher_t*, async::Task*, zx_status_t status) {
                    if (status == ZX_OK)
                      ShutDownInternal(true /* due_to_timeout */);
                  },
                  kTransactionTimeoutMs);
}

void Bearer::SendErrorResponse(OpCode request_opcode,
                               Handle attribute_handle,
                               ErrorCode error_code) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  auto buffer =
      common::NewSlabBuffer(sizeof(Header) + sizeof(ErrorResponseParams));
  FXL_CHECK(buffer);

  PacketWriter packet(kErrorResponse, buffer.get());
  auto* payload = packet.mutable_payload<ErrorResponseParams>();
  payload->request_opcode = request_opcode;
  payload->attribute_handle = htole16(attribute_handle);
  payload->error_code = error_code;

  chan_->Send(std::move(buffer));
}

void Bearer::HandleEndTransaction(TransactionQueue* tq,
                                  const PacketReader& packet) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(is_open());
  FXL_DCHECK(tq);

  if (!tq->current()) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "att: received unexpected transaction PDU (opcode: 0x%02x)",
        packet.opcode());
    ShutDown();
    return;
  }

  bool report_error = false;
  OpCode target_opcode;
  ErrorCode error_code = ErrorCode::kNoError;
  Handle attr_in_error = kInvalidHandle;

  if (packet.opcode() == kErrorResponse) {
    // We should never hit this branch for indications.
    FXL_DCHECK(tq->current()->opcode != kIndication);

    if (packet.payload_size() == sizeof(ErrorResponseParams)) {
      report_error = true;

      const auto& payload = packet.payload<ErrorResponseParams>();
      target_opcode = payload.request_opcode;
      error_code = payload.error_code;
      attr_in_error = le16toh(payload.attribute_handle);
    } else {
      FXL_VLOG(2) << "att: Received malformed error response";

      // Invalid opcode will fail the opcode comparison below.
      target_opcode = kInvalidOpCode;
    }
  } else {
    target_opcode = MatchingTransactionCode(packet.opcode());
  }

  FXL_DCHECK(tq->current()->opcode != kInvalidOpCode);

  if (tq->current()->opcode != target_opcode) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "att: Received bad transaction PDU (opcode: 0x%02x)", packet.opcode());
    ShutDown();
    return;
  }

  // The transaction is complete. Send out the next queued transaction and
  // notify the callback.
  auto transaction = tq->ClearCurrent();
  FXL_DCHECK(transaction);

  TryStartNextTransaction(tq);

  if (!report_error)
    transaction->callback(packet);
  else if (transaction->error_callback)
    transaction->error_callback(Status(error_code), attr_in_error);
}

Bearer::HandlerId Bearer::NextHandlerId() {
  auto id = next_handler_id_;

  // This will stop incrementing if this were overflows and always return
  // kInvalidHandlerId.
  if (next_handler_id_ != kInvalidHandlerId)
    next_handler_id_++;
  return id;
}

Bearer::TransactionId Bearer::NextRemoteTransactionId() {
  auto id = next_remote_transaction_id_;

  next_remote_transaction_id_++;

  // Increment extra in the case of overflow.
  if (next_remote_transaction_id_ == kInvalidTransactionId)
    next_remote_transaction_id_++;

  return id;
}

void Bearer::HandleBeginTransaction(RemoteTransaction* currently_pending,
                                    const PacketReader& packet) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(currently_pending);

  if (currently_pending->HasValue()) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "att: A transaction is already pending! (opcode: 0x%02x)",
        packet.opcode());
    ShutDown();
    return;
  }

  auto iter = handlers_.find(packet.opcode());
  if (iter == handlers_.end()) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "att: No handler registered for opcode 0x%02x", packet.opcode());
    SendErrorResponse(packet.opcode(), 0, ErrorCode::kRequestNotSupported);
    return;
  }

  auto id = NextRemoteTransactionId();
  *currently_pending = PendingRemoteTransaction(id, packet.opcode());

  iter->second(id, packet);
}

Bearer::RemoteTransaction* Bearer::FindRemoteTransaction(TransactionId id) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  if (remote_request_ && remote_request_->id == id) {
    return &remote_request_;
  }

  if (remote_indication_ && remote_indication_->id == id) {
    return &remote_indication_;
  }

  FXL_VLOG(1) << "att: id " << id << " does not match any transaction";
  return nullptr;
}

void Bearer::HandlePDUWithoutResponse(const PacketReader& packet) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  auto iter = handlers_.find(packet.opcode());
  if (iter == handlers_.end()) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "att: Dropping unhandled packet (opcode: 0x%02x)", packet.opcode());
    return;
  }

  iter->second(kInvalidTransactionId, packet);
}

void Bearer::OnChannelClosed() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  // This will deactivate the channel and notify |closed_cb_|.
  ShutDown();
}

void Bearer::OnRxBFrame(const l2cap::SDU& sdu) {
  FXL_DCHECK(is_open());
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  uint16_t length = sdu.length();

  // An ATT PDU should at least contain the opcode.
  if (length < sizeof(OpCode)) {
    FXL_VLOG(1) << "att: PDU too short!";
    ShutDown();
    return;
  }

  if (length > mtu_) {
    FXL_VLOG(1) << "att: PDU exceeds MTU!";
    ShutDown();
    return;
  }

  // The following will read the entire ATT PDU in a single call.
  l2cap::SDU::Reader reader(&sdu);
  reader.ReadNext(length, [this, length](const common::ByteBuffer& att_pdu) {
    FXL_CHECK(att_pdu.size() == length);
    PacketReader packet(&att_pdu);

    MethodType type = GetMethodType(packet.opcode());

    switch (type) {
      case MethodType::kResponse:
        HandleEndTransaction(&request_queue_, packet);
        break;
      case MethodType::kConfirmation:
        HandleEndTransaction(&indication_queue_, packet);
        break;
      case MethodType::kRequest:
        HandleBeginTransaction(&remote_request_, packet);
        break;
      case MethodType::kIndication:
        HandleBeginTransaction(&remote_indication_, packet);
        break;
      case MethodType::kNotification:
      case MethodType::kCommand:
        HandlePDUWithoutResponse(packet);
        break;
      default:
        FXL_VLOG(2) << fxl::StringPrintf("att: Unsupported opcode: 0x%02x",
                                         packet.opcode());
        SendErrorResponse(packet.opcode(), 0, ErrorCode::kRequestNotSupported);
        break;
    }
  });
}

}  // namespace att
}  // namespace btlib
