// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/packet_protocol/packet_protocol.h"

#include <cstdint>
#include <iostream>
#include <sstream>

#include "third_party/zlib/zlib.h"

namespace overnet {

namespace {

class AckSendRequest final : public PacketProtocol::SendRequest {
 public:
  AckSendRequest() : SendRequest(true) {}
  Slice GenerateBytes(LazySliceArgs args) override { return Slice(); }
  void Ack(const Status& status) override {}
};

AckSendRequest ack_send_request;

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// Initialization/shutdown/control paths

PacketProtocol::PacketProtocol(Timer* timer, RandFunc rand,
                               PacketSender* packet_sender, const Codec* codec,
                               uint64_t mss, bool probe_tails)
    : codec_(codec),
      timer_(timer),
      packet_sender_(packet_sender),
      probe_tails_(probe_tails),
      maximum_send_size_(mss) {
  state_.Reset(this, std::move(rand));
}

void PacketProtocol::Close(Callback<void> quiesced) {
  ScopedModule<PacketProtocol> in_pp(this);
  OVERNET_TRACE(DEBUG) << "Close";
  InTransaction([&](Transaction* transaction) {
    transaction->QuiesceOnCompletion(std::move(quiesced));
  });
}

PacketProtocol::AckSender::AckSender(PacketProtocol* protocol)
    : protocol_(protocol) {}

PacketProtocol::OutstandingMessages::OutstandingMessages(
    PacketProtocol* protocol)
    : protocol_(protocol) {}

void PacketProtocol::Quiesce() {
  ScopedModule<PacketProtocol> in_pp(this);
  OVERNET_TRACE(DEBUG) << "Quiesce";
  state_.Reset();
  primary_ref_.Drop();
}

PacketProtocol::Transaction::Transaction(PacketProtocol* protocol)
    : protocol_(protocol) {
  ScopedModule<PacketProtocol> in_pp(protocol_);
  OVERNET_TRACE(DEBUG) << "Transaction.Begin";
  assert(protocol_->active_transaction_ == nullptr);
  protocol_->active_transaction_ = this;
}

PacketProtocol::Transaction::~Transaction() {
  OVERNET_TRACE(DEBUG) << "Transaction.Finalize";
  ProtocolRef protocol(protocol_);
  for (;;) {
    if (quiesce_) {
      protocol->Quiesce();
    } else if (protocol->state_.has_value()) {
      if (bbr_ack_.has_value()) {
        protocol->state_->bbr.OnAck(bbr_ack_.Take());
        continue;
      } else if (set_tip_.has_value()) {
        auto set_tip = set_tip_.Take();
        protocol->state_->received_queue.SetTip(set_tip.seq_idx,
                                                set_tip.received);
        continue;
      } else if (start_sending_) {
        start_sending_ = false;
        protocol->state_->outstanding_messages.StartSending();
        continue;
      } else if (increment_outstanding_tip_) {
        increment_outstanding_tip_ = false;
        protocol->state_->outstanding_messages.IncrementTip();
        continue;
      }
    }

    OVERNET_TRACE(DEBUG) << "Transaction.End";
    assert(protocol->active_transaction_ == this);
    protocol->active_transaction_ = nullptr;
    return;
  }
}

void PacketProtocol::Transaction::QuiesceOnCompletion(Callback<void> callback) {
  OVERNET_TRACE(DEBUG) << "Schedule Quiesce";
  assert(!quiesce_);
  assert(protocol_->quiesce_.empty());
  protocol_->quiesce_ = std::move(callback);
  quiesce_ = true;
}

///////////////////////////////////////////////////////////////////////////////
// PacketProtocol::Send and friends.
// Defines the send path.

void PacketProtocol::Send(SendRequestHdl send_request) {
  ScopedModule<PacketProtocol> in_pp(this);
  OVERNET_TRACE(DEBUG) << "Send";
  InTransaction([&](Transaction* transaction) {
    if (!state_.has_value()) {
      return;
    }
    state_->outstanding_messages.Schedule(transaction, std::move(send_request));
  });
}

void PacketProtocol::OutstandingMessages::Schedule(Transaction* transaction,
                                                   SendRequestHdl hdl) {
  Schedule(transaction, OutstandingPacket::Pending{std::move(hdl)});
}

std::string PacketProtocol::OutstandingMessages::OutstandingString() const {
  std::ostringstream out;
  out << "{tip=" << send_tip_ << ":" << unsent_tip_ << "|";
  bool first = true;
  uint64_t idx = send_tip_;
  for (const auto& pkt : outstanding_) {
    if (!first) {
      out << ",";
    }
    first = false;
    out << idx << ":" << pkt.state;
    idx++;
  }
  out << "}";
  return out.str();
}

bool PacketProtocol::OutstandingMessages::HasPendingPackets(
    Transaction* transaction) const {
  for (auto i = unsent_tip_ - send_tip_; i != outstanding_.size(); i++) {
    if (outstanding_[i].is_pending()) {
      return true;
    }
  }
  return false;
}

void PacketProtocol::OutstandingMessages::Schedule(
    Transaction* transaction,
    OutstandingPacket::State outstanding_packet_state) {
  OVERNET_TRACE(DEBUG) << "OutstandingMessages.Schedule "
                       << outstanding_packet_state
                       << " outstanding=" << OutstandingString();
  tail_probe_timeout_.Reset();
  auto make_packet = [&] {
    return OutstandingPacket{std::move(outstanding_packet_state)};
  };
  if (!outstanding_.empty() &&
      std::holds_alternative<OutstandingPacket::PendingTailProbe>(
          outstanding_.back().state)) {
    outstanding_.back() = make_packet();
  } else {
    if (!HasPendingPackets(transaction)) {
      transaction->StartSendingOnCompletion();
    }
    outstanding_.emplace_back(make_packet());
    assert(HasPendingPackets(transaction));
  }
  max_outstanding_size_ =
      std::max(outstanding_.size(), size_t(max_outstanding_size_));
}

void PacketProtocol::OutstandingMessages::StartSending() {
  ScopedModule<PacketProtocol> in_pp(protocol_);
  OVERNET_TRACE(DEBUG) << "OutstandingMessages.StartSending: "
                       << OutstandingString();

  assert(!outstanding_.empty());
  assert(unsent_tip_ >= send_tip_);
  assert(unsent_tip_ - send_tip_ < outstanding_.size());

  transmit_request_.Reset(
      &protocol_->state_->bbr, [this](const Status& status) {
        OVERNET_TRACE(DEBUG) << "OutstandingMessages.Send status=" << status;
        if (status.is_error()) {
          return;
        }
        SeqNum seq_num(unsent_tip_, max_outstanding_size_ + 1);
        protocol_->packet_sender_->SendPacket(
            seq_num, PacketSend(protocol_, transmit_request_.Take()));
      });
}

PacketProtocol::PacketSend::PacketSend(PacketProtocol* protocol,
                                       BBR::TransmitRequest bbr_request)
    : protocol_(protocol), bbr_request_(std::move(bbr_request)) {
  assert(protocol);
}

Slice PacketProtocol::PacketSend::operator()(LazySliceArgs args) {
  auto protocol = std::move(protocol_);
  protocol->stats_.outgoing_packet_count++;
  return protocol->InTransaction(
      [=, protocol = protocol.get()](Transaction* transaction) {
        ScopedModule<PacketProtocol> in_pp(protocol);
        Slice output;
        if (protocol->state_.has_value()) {
          output = protocol->state_->outstanding_messages.GeneratePacket(
              transaction, std::move(bbr_request_), args);
        }
        if (protocol->state_.has_value()) {
          protocol->state_->outstanding_messages.SentMessage(transaction);
        }
        return output;
      });
}

PacketProtocol::PacketSend::~PacketSend() {
  if (protocol_.has_value() && protocol_->state_.has_value()) {
    protocol_->InTransaction([this](Transaction* transaction) {
      protocol_->state_->outstanding_messages.CancelledMessage(transaction);
    });
  }
}

void PacketProtocol::OutstandingMessages::CancelledMessage(
    Transaction* transaction) {
  protocol_->state_->connectivity_detection.FailedDelivery();
  NackAll(transaction);
  SentMessage(transaction);
}

Slice PacketProtocol::OutstandingMessages::GeneratePacket(
    Transaction* transaction, BBR::TransmitRequest bbr_request,
    LazySliceArgs args) {
  uint64_t seq_idx;
  for (seq_idx = unsent_tip_; seq_idx - send_tip_ != outstanding_.size() &&
                              !outstanding_[seq_idx - send_tip_].is_pending();
       seq_idx++) {
  }

  if (seq_idx - send_tip_ == outstanding_.size()) {
    // nack before send?
    OVERNET_TRACE(DEBUG) << "Seq " << unsent_tip_ << " nacked before sending";
    return Slice();
  }

  auto* outstanding_packet = &outstanding_[seq_idx - send_tip_];

  OVERNET_TRACE(DEBUG) << "GeneratePacket: " << outstanding_packet->state;

  SendRequestHdl request;
  Slice send;

  if (std::holds_alternative<OutstandingPacket::Pending>(
          outstanding_packet->state)) {
    auto& pending_packet =
        std::get<OutstandingPacket::Pending>(outstanding_packet->state);
    request = std::move(pending_packet.request);
    last_send_was_tail_probe_ = false;
  } else if (std::holds_alternative<OutstandingPacket::PendingTailProbe>(
                 outstanding_packet->state)) {
    request = SendRequestHdl(&ack_send_request);
    last_send_was_tail_probe_ = true;
  } else {
    abort();
  }

  bool has_ack;
  auto send_status = protocol_->codec_->Encode(
      seq_idx, protocol_->FormatPacket(transaction, seq_idx, request.borrow(),
                                       args, &has_ack));
  if (send_status.is_error()) {
    OVERNET_TRACE(ERROR) << "Failed to encode packet: "
                         << send_status.AsStatus();
  } else {
    send = std::move(*send_status);
  }

  // outstanding_ should not have changed
  assert(outstanding_packet == &outstanding_[seq_idx - send_tip_]);
  outstanding_packet->state = OutstandingPacket::Sent{
      has_ack ? protocol_->state_->received_queue.FirstUnknownSequence() : 0,
      std::move(request),
      bbr_request.Sent(BBR::OutgoingPacket{seq_idx, send.length()}),
      protocol_->state_->bdp_estimator.SentPacket(seq_idx)};
  unsent_tip_++;
  ScheduleRetransmit();
  return send;
}

Slice PacketProtocol::FormatPacket(Transaction* transaction, uint64_t seq_idx,
                                   SendRequest* request, LazySliceArgs args,
                                   bool* has_ack) {
  assert(state_.has_value());

  const auto max_length = std::min(static_cast<uint64_t>(args.max_length),
                                   static_cast<uint64_t>(maximum_send_size_));
  auto make_args = [=](uint64_t prefix_length, bool has_other_content) {
    auto maxlen = max_length;
    auto subtract_from_max =
        std::min(maxlen, prefix_length + codec_->border.Total());
    maxlen -= subtract_from_max;
    return LazySliceArgs{args.desired_border.WithAddedPrefix(
                             prefix_length + codec_->border.prefix),
                         maxlen, has_other_content || args.has_other_content};
  };

  OVERNET_TRACE(DEBUG) << "FormatPacket: can_build_ack="
                       << state_->received_queue.CanBuildAck()
                       << " request.must_send_ack=" << request->must_send_ack()
                       << " should_send_ack="
                       << state_->ack_sender.ShouldSendAck()
                       << " acksplanation="
                       << state_->ack_sender.Acksplanation();

  if (state_->received_queue.CanBuildAck() &&
      (request->must_send_ack() || state_->ack_sender.ShouldSendAck())) {
    stats_.acks_sent++;
    auto ack = state_->received_queue.BuildAck(
        transaction, seq_idx, timer_->Now(),
        varint::MaximumLengthWithPrefix(max_length), &state_->ack_sender);
    AckFrame::Writer ack_writer(&ack);
    const uint8_t ack_length_length =
        varint::WireSizeFor(ack_writer.wire_length());
    const uint64_t prefix_length = ack_length_length + ack_writer.wire_length();
    auto payload = request->GenerateBytes(make_args(prefix_length, true));
    stats_.pure_acks_sent += payload.length() == 0;
    *has_ack = true;
    return std::move(payload).WithPrefix(
        prefix_length, [&ack_writer, ack_length_length](uint8_t* p) {
          ack_writer.Write(
              varint::Write(ack_writer.wire_length(), ack_length_length, p));
        });
  } else {
    *has_ack = false;
    return request->GenerateBytes(make_args(1, false))
        .WithPrefix(1, [](uint8_t* p) { *p = 0; });
  }
}

uint64_t PacketProtocol::ReceivedQueue::MaxSeenSequence() const {
  if (received_packets_.empty()) {
    return received_tip_ - 1;
  }
  size_t idx = received_packets_.size() - 1;
  while (idx > 0 && received_packets_[idx].state == ReceiveState::UNKNOWN) {
    idx--;
  }
  return received_tip_ + idx;
}

uint64_t PacketProtocol::ReceivedQueue::FirstUnknownSequence() const {
  size_t idx;
  for (idx = 0; idx < received_packets_.size() &&
                received_packets_[idx].state != ReceiveState::UNKNOWN;
       idx++) {
  }
  return received_tip_ + idx;
}

AckFrame PacketProtocol::ReceivedQueue::BuildAck(Transaction* transaction,
                                                 uint64_t seq_idx,
                                                 TimeStamp now,
                                                 uint32_t max_length,
                                                 AckSender* ack_sender) {
  OVERNET_TRACE(DEBUG) << "BuildAck seq:" << seq_idx
                       << " from received packets: " << ReceivedPacketSummary();

  auto packet_delay = [this, now](uint64_t seq_idx) -> uint64_t {
    const auto& received_packet = received_packets_[seq_idx - received_tip_];
    assert(received_packet.state != ReceiveState::UNKNOWN);
    return (now - received_packet.when).as_us();
  };

  const auto max_seen = MaxSeenSequence();
  assert(max_seen > 0);

  OVERNET_TRACE(DEBUG) << "  max_seen=" << max_seen
                       << " received_tip=" << received_tip_;

  AckFrame ack(max_seen, packet_delay(max_seen));
  for (uint64_t seq_idx = max_seen; seq_idx >= received_tip_; seq_idx--) {
    auto& received_packet = received_packets_[seq_idx - received_tip_];
    switch (received_packet.state) {
      case ReceiveState::UNKNOWN:
        OVERNET_TRACE(DEBUG)
            << "Mark unseen packet " << seq_idx << " as NOT_RECEIVED";
        received_packet = ReceivedPacket{ReceiveState::NOT_RECEIVED, now};
        stats_->unseen_packets_marked_not_received++;
        [[fallthrough]];
      case ReceiveState::NOT_RECEIVED:
        ack.AddNack(seq_idx);
        break;
      case ReceiveState::RECEIVED:
      case ReceiveState::RECEIVED_PURE_ACK:
      case ReceiveState::RECEIVED_AND_ACKED_IMMEDIATELY:
        break;
    }
  }

  ack.AdjustForMSS(max_length, packet_delay);

  ack_sender->AckSent(transaction, seq_idx, ack.partial());

  OVERNET_TRACE(DEBUG) << "BuildAck generates: " << ack << " bytes="
                       << Slice::FromWriters(AckFrame::Writer(&ack));

  return ack;
}

void PacketProtocol::OutstandingMessages::SentMessage(
    Transaction* transaction) {
  ScopedModule<PacketProtocol> in_pp(protocol_);
  OVERNET_TRACE(DEBUG) << "OutstandingMessages.SentMessage: "
                       << OutstandingString();
  if (HasPendingPackets(transaction)) {
    transaction->StartSendingOnCompletion();
  }
  if (protocol_->probe_tails_ && !last_send_was_tail_probe_) {
    ScheduleAck();
  }
}

void PacketProtocol::OutstandingMessages::ScheduleAck() {
  ScopedModule<PacketProtocol> in_pp(protocol_);
  const auto when = protocol_->timer_->Now() + protocol_->TailLossProbeDelay();
  if (unsent_tip_ - send_tip_ != outstanding_.size()) {
    protocol_->stats_
        .tail_loss_probes_cancelled_because_requests_already_queued++;
    OVERNET_TRACE(DEBUG)
        << "OutstandingMessages.ScheduleTailLossProbe - already queued; "
        << OutstandingString();
    return;
  }
  if (tail_probe_timeout_.has_value() && tail_probe_timeout_->when <= when) {
    OVERNET_TRACE(DEBUG)
        << "OutstandingMessages.ScheduleTailLossProbe - already scheduled";
    protocol_->stats_
        .tail_loss_probes_cancelled_because_probe_already_scheduled++;
    return;
  }
  OVERNET_TRACE(DEBUG) << "OutstandingMessages.ScheduleTailLossProbe @ "
                       << when;
  tail_probe_timeout_.Reset(
      protocol_->timer_, when, [this](const Status& status) {
        ScopedModule<PacketProtocol> in_pp(protocol_);
        OVERNET_TRACE(DEBUG)
            << "OutstandingMessages.ScheduleTailLossProbe --> " << status;
        if (status.is_error()) {
          protocol_->stats_.tail_loss_probes_cancelled_after_timer_created++;
          return;
        }
        tail_probe_timeout_.Reset();
        protocol_->stats_.tail_loss_probes_scheduled++;
        protocol_->InTransaction(
            [this](Transaction* transaction) { ForceSendAck(transaction); });
      });
}

void PacketProtocol::OutstandingMessages::ForceSendAck(
    Transaction* transaction) {
  OVERNET_TRACE(DEBUG) << "OutstandingMessages.ForceSendAck";
  Schedule(transaction, OutstandingPacket::PendingTailProbe{});
}

///////////////////////////////////////////////////////////////////////////////
// PacketProtocol::Process and friends.
// Defines the receive path.

void PacketProtocol::Process(TimeStamp received, SeqNum seq_num, Slice slice,
                             ProcessCallback handle_message) {
  assert(refs_);
  ScopedModule<PacketProtocol> scoped_module(this);
  Transaction transaction(this);

  OVERNET_TRACE(DEBUG) << "Process: " << slice;

  stats_.incoming_packet_count++;

  if (!state_.has_value()) {
    return;
  }

  state_->connectivity_detection.MessageReceived();
  state_->bdp_estimator.ReceivedBytes(slice.length());
  state_->ack_sender.NeedAck(
      &transaction,
      state_->received_queue.Received(seq_num, received, [&](uint64_t seq_idx) {
        return ProcessMessage(&transaction, seq_idx, std::move(slice), received,
                              std::move(handle_message));
      }));
  state_->outstanding_messages.ReceivedPacket();
}

template <class F>
PacketProtocol::AckUrgency PacketProtocol::ReceivedQueue::Received(
    SeqNum seq_num, TimeStamp received, F logic) {
  const auto window_base = MaxSeenSequence();
  const auto seq_idx = seq_num.Reconstruct(window_base);

  OVERNET_TRACE(DEBUG) << "Process seq:" << seq_idx
                       << " (from window_base:" << window_base << ")";

  if (seq_idx < received_tip_) {
    stats_->ack_not_required_historic_sequence++;
    return AckUrgency::NOT_REQUIRED;
  }

  if (!EnsureValidReceivedPacket(seq_idx, received)) {
    return AckUrgency::NOT_REQUIRED;
  }

  auto* received_packet = &received_packets_[seq_idx - received_tip_];
  if (received_packet->state != ReceiveState::UNKNOWN) {
    OVERNET_TRACE(DEBUG) << "frozen as " << received_packet->state;
    stats_->ack_not_required_frozen_sequence++;
    return AckUrgency::NOT_REQUIRED;
  }

  auto pmr = logic(seq_idx);
  OVERNET_TRACE(DEBUG) << "Process seq:" << seq_idx
                       << " process_message_result=" << pmr
                       << " optional_ack_run_length="
                       << optional_ack_run_length_
                       << " received_packets=" << ReceivedPacketSummary();

  // received_packet may not be valid anymore, but the packet *must* exist
  assert(seq_idx >= received_tip_);
  received_packet = &received_packets_[seq_idx - received_tip_];

  switch (pmr) {
    case ProcessMessageResult::NOT_PROCESSED:
      // Failed processing packets shouldn't generate traffic.
      stats_->ack_not_required_invalid_packet++;
      return AckUrgency::NOT_REQUIRED;
    case ProcessMessageResult::NACK:
      optional_ack_run_length_ = 0;
      *received_packet = ReceivedPacket{ReceiveState::NOT_RECEIVED, received};
      stats_->ack_required_immediately_due_to_nack++;
      // Always send a nack as soon as we realize one is necessary.
      return AckUrgency::SEND_IMMEDIATELY;
    case ProcessMessageResult::OPTIONAL_ACK:
      // If we get a single ack without a payload, we suppress sending a reply
      // ack.
      optional_ack_run_length_++;
      *received_packet =
          ReceivedPacket{ReceiveState::RECEIVED_PURE_ACK, received};
      if (optional_ack_run_length_ < 5) {
        stats_->ack_not_required_short_optional_run++;
        return AckUrgency::NOT_REQUIRED;
      }
      stats_->ack_required_soon_ack_received++;
      optional_ack_run_length_ = 0;
      return AckUrgency::SEND_BUNDLED;
    case ProcessMessageResult::ACK: {
      optional_ack_run_length_ = 0;
      *received_packet = ReceivedPacket{ReceiveState::RECEIVED, received};
      static const uint64_t kMaxIncomingBeforeForcedAck = 3;
      if (seq_idx - received_tip_ >= kMaxIncomingBeforeForcedAck) {
        bool any_packets_received = false;
        bool any_packets_unknown = false;
        for (auto idx = seq_idx - received_tip_ - kMaxIncomingBeforeForcedAck;
             idx < seq_idx - received_tip_; idx++) {
          switch (received_packets_[idx].state) {
            case ReceiveState::RECEIVED:
            case ReceiveState::RECEIVED_AND_ACKED_IMMEDIATELY:
              any_packets_received = true;
              break;
            case ReceiveState::UNKNOWN:
              any_packets_unknown = true;
              break;
            case ReceiveState::NOT_RECEIVED:
            case ReceiveState::RECEIVED_PURE_ACK:
              break;
          }
        }
        if (any_packets_unknown || !any_packets_received) {
          *received_packet = ReceivedPacket{
              ReceiveState::RECEIVED_AND_ACKED_IMMEDIATELY, received};
          stats_->ack_required_immediately_due_to_multiple_receives++;
          return AckUrgency::SEND_IMMEDIATELY;
        }
      }
      // Got some data, make sure there's an ack scheduled soon.
      stats_->ack_required_soon_data_received++;
      return AckUrgency::SEND_SOON;
    }
    case ProcessMessageResult::ACK_URGENTLY: {
      optional_ack_run_length_ = 0;
      *received_packet = ReceivedPacket{ReceiveState::RECEIVED, received};
      stats_->ack_required_immediately_due_to_partial_ack++;
      return AckUrgency::SEND_IMMEDIATELY;
    }
  }
}

std::string PacketProtocol::ReceivedQueue::ReceivedPacketSummary() const {
  std::ostringstream out;
  out << "{";
  uint64_t seq_idx = received_tip_;
  for (const auto& pkt : received_packets_) {
    if (seq_idx != received_tip_) {
      out << ", ";
    }
    out << "[" << seq_idx << "] " << pkt.state;
    seq_idx++;
  }
  out << "}";
  return out.str();
}

PacketProtocol::ProcessMessageResult PacketProtocol::ProcessMessage(
    Transaction* transaction, uint64_t seq_idx, Slice slice, TimeStamp received,
    ProcessCallback handle_message) {
  using StatusType = StatusOr<IncomingMessage*>;

  // Decode slice
  auto slice_status = codec_->Decode(seq_idx, std::move(slice));
  if (slice_status.is_error()) {
    handle_message(slice_status.AsStatus());
    return ProcessMessageResult::NOT_PROCESSED;
  }
  slice = std::move(*slice_status);

  const uint8_t* p = slice.begin();
  const uint8_t* end = slice.end();

  if (p == end) {
    // Empty packet
    handle_message(nullptr);
    return ProcessMessageResult::OPTIONAL_ACK;
  }

  uint64_t ack_length;
  if (!varint::Read(&p, end, &ack_length)) {
    handle_message(StatusType(StatusCode::INVALID_ARGUMENT,
                              "Failed to parse ack length from message"));
    return ProcessMessageResult::NOT_PROCESSED;
  }
  slice.TrimBegin(p - slice.begin());

  OVERNET_TRACE(DEBUG) << "ack_length=" << ack_length;

  if (ack_length > slice.length()) {
    handle_message(StatusType(StatusCode::INVALID_ARGUMENT,
                              "Ack frame claimed to be past end of message"));
    return ProcessMessageResult::NOT_PROCESSED;
  }

  Optional<AckFrame> ack;

  ProcessMessageResult ack_result = ProcessMessageResult::OPTIONAL_ACK;
  if (ack_length > 0) {
    auto ack_status = AckFrame::Parse(slice.TakeUntilOffset(ack_length));
    if (ack_status.is_error()) {
      handle_message(ack_status.AsStatus());
      return ProcessMessageResult::NOT_PROCESSED;
    }

    if (auto ack_valid = state_->outstanding_messages.ValidateAck(*ack_status);
        ack_valid.is_error()) {
      handle_message(ack_valid);
      return ProcessMessageResult::NACK;
    }

    OVERNET_TRACE(DEBUG) << "Process seq:" << seq_idx
                         << " got-ack:" << *ack_status;
    ack = std::move(*ack_status);
    if (ack->partial()) {
      ack_result = ProcessMessageResult::ACK_URGENTLY;
    }
  }

  if (slice.length() > 0) {
    IncomingMessage msg(std::move(slice));

    // Process the message body:
    handle_message(&msg);

    if (msg.was_nacked()) {
      // Note: ack not processed
      return ProcessMessageResult::NACK;
    } else if (ack_result != ProcessMessageResult::ACK_URGENTLY) {
      ack_result = ProcessMessageResult::ACK;
    }
  } else {
    // Handle no message:
    handle_message(nullptr);
  }
  if (ack.has_value()) {
    state_->outstanding_messages.ProcessValidAck(transaction, ack.Take(),
                                                 received);
  }
  return ack_result;
}

Status PacketProtocol::OutstandingMessages::ValidateAck(
    const AckFrame& ack) const {
  if (ack.ack_to_seq() < send_tip_) {
    return Status::Ok();
  }
  if (ack.ack_to_seq() >= send_tip_ + outstanding_.size()) {
    std::ostringstream msg;
    msg << "Ack packet past sending sequence: ack_seq=" << ack.ack_to_seq()
        << " max_sent=" << (send_tip_ + outstanding_.size() - 1)
        << " outstanding_window=" << outstanding_.size();
    return Status(StatusCode::INVALID_ARGUMENT, msg.str());
  }
  for (auto nack_seq : ack.nack_seqs()) {
    if (nack_seq < send_tip_) {
      continue;
    }
    const OutstandingPacket& pkt = outstanding_[nack_seq - send_tip_];
    if (std::holds_alternative<OutstandingPacket::Acked>(pkt.state)) {
      // Previously acked packet becomes nacked: this is an error.
      return Status(StatusCode::INVALID_ARGUMENT,
                    "Previously acked packet becomes nacked");
    }
  }
  for (size_t i = 0; i < ack.ack_to_seq() - send_tip_; i++) {
    if (std::holds_alternative<OutstandingPacket::Pending>(
            outstanding_[i].state)) {
      return Status(StatusCode::INVALID_ARGUMENT, "Ack/nack unsent sequence");
    }
  }
  return Status::Ok();
}

void PacketProtocol::OutstandingMessages::ProcessValidAck(
    Transaction* transaction, AckFrame ack, TimeStamp received) {
  // Basic validation. Can assert anything that should be an error because
  // ValidateAck should have been called prior.
  if (ack.ack_to_seq() < send_tip_) {
    return;
  }
  assert(ack.ack_to_seq() < send_tip_ + outstanding_.size());

  const auto queue_delay = TimeDelta::FromMicroseconds(ack.ack_delay_us());

  // Fail any nacked packets.
  // Iteration is from oldest packet to newest, such that the OLDEST nacked
  // message is the most likely to be sent first. This has the important
  // consequence that if the packet was a fragment of a large message that was
  // rejected due to buffering, the earlier pieces (that are more likely to
  // fit) are retransmitted first.
  for (auto nack_seq : ack.nack_seqs()) {
    Nack(transaction, nack_seq, queue_delay, Status::Unavailable());
  }

  // Clear out outstanding packet references, propagating acks.
  for (size_t i = send_tip_; i <= ack.ack_to_seq(); i++) {
    OutstandingPacket& pkt = outstanding_[i - send_tip_];
    if (std::holds_alternative<OutstandingPacket::Sent>(pkt.state)) {
      Ack(transaction, i, queue_delay, received);
    }
  }
}

void PacketProtocol::OutstandingMessages::Ack(Transaction* transaction,
                                              uint64_t ack_seq,
                                              TimeDelta queue_delay,
                                              TimeStamp received) {
  OutstandingPacket& pkt = outstanding_[ack_seq - send_tip_];
  auto& sent_packet = std::get<OutstandingPacket::Sent>(pkt.state);
  auto request = std::move(sent_packet.request);
  auto& bbr_sent_packet = sent_packet.bbr_sent_packet;
  bbr_sent_packet.send_time += queue_delay;
  transaction->QueueAck(bbr_sent_packet);
  if (sent_packet.first_unknown_sequence_at_send > 1) {
    // Move receive window forward.
    transaction->SetTip(sent_packet.first_unknown_sequence_at_send - 1,
                        received);
  }
  protocol_->state_->bdp_estimator.AckPacket(sent_packet.bdp_packet);
  protocol_->state_->ack_sender.OnAck(ack_seq);
  pkt.state = OutstandingPacket::Acked{};
  transaction->IncrementOutstandingTipOnCompletion();
  request.Ack(Status::Ok());
}

void PacketProtocol::OutstandingMessages::Nack(Transaction* transaction,
                                               uint64_t nack_seq,
                                               TimeDelta queue_delay,
                                               const Status& status) {
  OVERNET_TRACE(DEBUG) << "AckProcessor.Nack: seq=" << nack_seq
                       << " status=" << status << " send_tip=" << send_tip_;
  assert(status.is_error());
  if (protocol_->state_.has_value()) {
    protocol_->state_->ack_sender.OnNack(
        transaction, nack_seq, status.code() == StatusCode::CANCELLED);
  }
  if (nack_seq < send_tip_) {
    return;
  }
  OutstandingPacket& pkt = outstanding_[nack_seq - send_tip_];
  OVERNET_TRACE(DEBUG) << "AckProcessor.Nack: seq=" << nack_seq
                       << " state=" << pkt.state;
  if (std::holds_alternative<OutstandingPacket::Pending>(pkt.state)) {
    pkt.state = OutstandingPacket::Nacked{};
    transaction->IncrementOutstandingTipOnCompletion();
  } else if (std::holds_alternative<OutstandingPacket::PendingTailProbe>(
                 pkt.state)) {
    pkt.state = OutstandingPacket::Nacked{};
    transaction->IncrementOutstandingTipOnCompletion();
    if (protocol_->state_.has_value()) {
      protocol_->state_->outstanding_messages.ForceSendAck(transaction);
    }
  } else if (auto* sent_packet =
                 std::get_if<OutstandingPacket::Sent>(&pkt.state)) {
    assert(sent_packet->bbr_sent_packet.outgoing.sequence == nack_seq);
    auto& bbr_sent_packet = sent_packet->bbr_sent_packet;
    bbr_sent_packet.send_time += queue_delay;
    transaction->QueueNack(bbr_sent_packet);
    auto request = std::move(sent_packet->request);
    pkt.state = OutstandingPacket::Nacked{};
    transaction->IncrementOutstandingTipOnCompletion();
    request.Ack(status);
  } else if (std::holds_alternative<OutstandingPacket::Nacked>(pkt.state)) {
  } else {
    // Previously acked packet becomes nacked: this is an error that should be
    // diagnosed during validation.
    abort();
  }
}

void PacketProtocol::OutstandingMessages::IncrementTip() {
  while (!outstanding_.empty() && outstanding_.front().is_finalized()) {
    send_tip_++;
    outstanding_.pop_front();
  }
  if (unsent_tip_ < send_tip_) {
    unsent_tip_ = send_tip_;
  }
}

void PacketProtocol::ReceivedQueue::SetTip(uint64_t seq_idx,
                                           TimeStamp received) {
  OVERNET_TRACE(DEBUG) << "SetTip from " << received_tip_ << " to before seq "
                       << seq_idx << " with received packets "
                       << ReceivedPacketSummary();
  assert(seq_idx >= 1);
  uint64_t tip_idx = seq_idx - 1;
  if (tip_idx <= received_tip_) {
    return;
  }
  if (!EnsureValidReceivedPacket(tip_idx, received)) {
    abort();
  }
  auto remove_begin = received_packets_.begin();
  auto remove_end = remove_begin + (tip_idx - received_tip_);
  for (auto it = remove_begin; it != remove_end; ++it) {
    assert(it->state != ReceiveState::UNKNOWN);
  }
  received_packets_.erase(remove_begin, remove_end);
  received_tip_ = tip_idx;
}

///////////////////////////////////////////////////////////////////////////////
// Ack scheduling

std::string PacketProtocol::AckSender::Acksplanation() const {
  std::ostringstream out;
  out << "{sent_full_acks=" << SentFullAcksString()
      << " all_acks_acked=" << all_acks_acknowledged_
      << " suppress_need_acks=" << suppress_need_acks_
      << " urgency=" << urgency_ << " set=" << urgency_set_
      << " send_ack_timer=" << send_ack_timer_.has_value() << "}";
  return out.str();
}

std::string PacketProtocol::AckSender::SentFullAcksString() const {
  std::ostringstream out;
  bool first = true;
  for (uint64_t ack : sent_full_acks_) {
    if (!first) {
      out << ",";
    }
    first = false;
    out << ack;
  }
  return "{" + out.str() + "}";
}

void PacketProtocol::AckSender::NeedAck(Transaction* transaction,
                                        AckUrgency urgency) {
  OVERNET_TRACE(DEBUG) << "AckSender.NeedAck"
                       << " urgency=" << urgency << " " << Acksplanation();

  if (urgency <= urgency_) {
    return;
  }

  const auto old_urgency = urgency_;
  urgency_ = urgency;
  urgency_set_ = protocol_->timer_->Now();
  assert(protocol_->state_.has_value());
  sent_full_acks_.clear();
  all_acks_acknowledged_ = false;
  switch (urgency_) {
    case AckUrgency::NOT_REQUIRED:
      abort();
    case AckUrgency::SEND_BUNDLED:
      suppress_need_acks_ = false;
      break;
    case AckUrgency::SEND_SOON: {
      const auto when = urgency_set_ + protocol_->TailLossProbeDelay();
      OVERNET_TRACE(DEBUG) << "AckSender.NeedAck: schedule ack start for "
                           << when;
      if (old_urgency != AckUrgency::SEND_BUNDLED) {
        suppress_need_acks_ = true;
      }
      send_ack_timer_.Reset(protocol_->timer_, when, [this](const Status& status) {
        ScopedModule in_pp(protocol_);
        OVERNET_TRACE(DEBUG) << "AckSender.NeedAck: ack start --> " << status;
        if (status.is_error()) {
          return;
        }
        suppress_need_acks_ = false;
        protocol_->stats_
            .tail_loss_probe_scheduled_because_ack_required_soon_timer_expired++;
        protocol_->state_->outstanding_messages.ScheduleAck();
      });
    } break;
    case AckUrgency::SEND_IMMEDIATELY:
      suppress_need_acks_ = false;
      send_ack_timer_.Reset();
      protocol_->state_->outstanding_messages.ForceSendAck(transaction);
      break;
  }
}

void PacketProtocol::AckSender::AckSent(Transaction* transaction,
                                        uint64_t seq_idx, bool partial) {
  OVERNET_TRACE(DEBUG) << "AckSender.AckSent seq_idx=" << seq_idx
                       << " partial=" << partial << " " << Acksplanation();

  send_ack_timer_.Reset();
  if (!sent_full_acks_.empty()) {
    assert(seq_idx > sent_full_acks_.back());
  }
  urgency_ = AckUrgency::NOT_REQUIRED;
  if (!partial) {
    sent_full_acks_.push_back(seq_idx);
  } else if (sent_full_acks_.empty()) {
    protocol_->stats_.ack_required_soon_continue_partial_after_ack++;
    NeedAck(transaction, AckUrgency::SEND_SOON);
  }
}

void PacketProtocol::AckSender::OnNack(Transaction* transaction, uint64_t seq,
                                       bool shutting_down) {
  OVERNET_TRACE(DEBUG) << "AckSender.OnNack"
                       << " seq=" << seq << " " << Acksplanation();
  auto it =
      std::lower_bound(sent_full_acks_.begin(), sent_full_acks_.end(), seq);
  if (it == sent_full_acks_.end() || *it != seq) {
    return;
  }
  sent_full_acks_.erase(it);
  if (sent_full_acks_.empty() && !shutting_down) {
    protocol_->stats_.ack_required_soon_all_acks_nacked++;
    NeedAck(transaction, AckUrgency::SEND_BUNDLED);
  }
}

void PacketProtocol::AckSender::OnAck(uint64_t seq) {
  OVERNET_TRACE(DEBUG) << "AckSender.OnAck"
                       << " seq=" << seq << " " << Acksplanation();
  auto it =
      std::lower_bound(sent_full_acks_.begin(), sent_full_acks_.end(), seq);
  if (it == sent_full_acks_.end() || *it != seq) {
    return;
  }
  sent_full_acks_.clear();
  all_acks_acknowledged_ = true;
}

///////////////////////////////////////////////////////////////////////////////
// Retransmit scheduling

void PacketProtocol::OutstandingMessages::ScheduleRetransmit() {
  OVERNET_TRACE(DEBUG) << "OutstandingMessages.ScheduleRetransmit: rto_timer="
                       << retransmit_timeout_.has_value()
                       << " deadline=" << RetransmitDeadline();
  if (retransmit_timeout_.has_value()) {
    return;
  }
  if (auto timeout = RetransmitDeadline(); timeout.has_value()) {
    retransmit_timeout_.Reset(
        protocol_->timer_, *timeout,
        [protocol = protocol_](const Status& status) {
          ScopedModule in_pp(protocol);
          protocol->InTransaction([=](Transaction* transaction) {
            OVERNET_TRACE(DEBUG)
                << "OutstandingMessages.ScheduleRetransmit --> " << status
                << " protocol_open=" << !transaction->Closing();
            if (transaction->Closing()) {
              return;
            }
            if (status.is_error()) {
              protocol->state_->outstanding_messages.NackAll(transaction);
              return;
            }
            protocol->state_->outstanding_messages.CheckRetransmit(transaction);
          });
        });
  }
}

Optional<TimeStamp> PacketProtocol::OutstandingMessages::RetransmitDeadline() {
  OVERNET_TRACE(DEBUG) << "OutstandingMessages.RetransmitDeadline: "
                       << OutstandingString();
  for (const auto& outstanding : outstanding_) {
    if (const auto* sent_packet =
            std::get_if<OutstandingPacket::Sent>(&outstanding.state)) {
      return sent_packet->bbr_sent_packet.send_time +
             protocol_->RetransmitDelay();
    }
  }
  return Nothing;
}

void PacketProtocol::OutstandingMessages::CheckRetransmit(
    Transaction* transaction) {
  if (!protocol_->state_.has_value()) {
    return;
  }
  retransmit_timeout_.Reset();
  const auto nack_before =
      protocol_->timer_->Now() - protocol_->RetransmitDelay();
  OVERNET_TRACE(DEBUG) << "OutstandingMessages.CheckRetransmit: nack_before="
                       << nack_before
                       << " current_rtt=" << protocol_->CurrentRTT()
                       << " retransmit_delay=" << protocol_->RetransmitDelay()
                       << " outstanding=" << OutstandingString();
  for (size_t i = 0; i < outstanding_.size(); i++) {
    if (outstanding_[i].is_finalized()) {
      OVERNET_TRACE(DEBUG) << "OutstandingMessages.CheckRetransmit: seq "
                           << (send_tip_ + i) << " finalized: STOP";
      break;
    }
    if (auto* p =
            std::get_if<OutstandingPacket::Sent>(&outstanding_[i].state)) {
      const auto sent = p->bbr_sent_packet.send_time;
      if (sent > nack_before) {
        OVERNET_TRACE(DEBUG)
            << "OutstandingMessages.CheckRetransmit: seq " << (send_tip_ + i)
            << " sent at " << sent << ": STOP";
        break;
      }
      OVERNET_TRACE(DEBUG) << "OutstandingMessages.CheckRetransmit: seq "
                           << (send_tip_ + i) << " sent at " << sent
                           << ": NACK";
      protocol_->state_->connectivity_detection.FailedDelivery();
      Nack(transaction, send_tip_ + i, TimeDelta::Zero(),
           Status::Unavailable());
    }
  }
  ScheduleRetransmit();
}

PacketProtocol::OutstandingMessages::~OutstandingMessages() {
  protocol_->InTransaction(
      [this](Transaction* transaction) { NackAll(transaction); });
}

void PacketProtocol::OutstandingMessages::NackAll(Transaction* transaction) {
  OVERNET_TRACE(DEBUG) << "OutstandingMessages.NackAll";
  for (uint64_t i = send_tip_, end = send_tip_ + outstanding_.size(); i < end;
       i++) {
    if (outstanding_[i - send_tip_].is_finalized()) {
      continue;
    }
    Nack(transaction, i, TimeDelta::Zero(), Status::Cancelled());
  }
}

///////////////////////////////////////////////////////////////////////////////
// Connectivity Detection

void PacketProtocol::ConnectivityDetection::FailedDelivery() {
  if (no_route_timeout_.has_value()) {
    return;
  }
  OVERNET_TRACE(DEBUG) << "ConnectivityDetection: Failed Delivery";
  no_route_timeout_.Reset(
      protocol_->timer_, protocol_->timer_->Now() + TimeDelta::FromSeconds(120),
      [this](const Status& status) {
        ScopedModule<PacketProtocol> in_pp(protocol_);
        if (status.is_error()) {
          OVERNET_TRACE(DEBUG) << "ConnectivityDetection: Timeout " << status;
          return;
        }
        OVERNET_TRACE(DEBUG)
            << "ConnectivityDetection: Signal loss of connectivity";
        if (protocol_->state_.has_value()) {
          protocol_->InTransaction([this](Transaction* transaction) {
            protocol_->packet_sender_->NoConnectivity();
          });
        }
      });
}

///////////////////////////////////////////////////////////////////////////////
// Utilities

TimeDelta PacketProtocol::CurrentRTT() const {
  return std::max(TimeDelta::FromMilliseconds(1), state_->bbr.rtt());
}

TimeDelta PacketProtocol::RetransmitDelay() const {
  constexpr const auto kMinRetransmitDelay = TimeDelta::FromSeconds(1);
  constexpr const int kRTTScaling = 4;
  auto rtt = CurrentRTT();
  if (rtt == TimeDelta::PositiveInf() ||
      rtt < kMinRetransmitDelay / kRTTScaling) {
    return kMinRetransmitDelay;
  }
  return kRTTScaling * rtt;
}

TimeDelta PacketProtocol::TailLossProbeDelay() const {
  constexpr const auto kMinTailLossProbeDelay = TimeDelta::FromMilliseconds(1);
  constexpr const int kRTTScaling = 4;
  auto rtt = CurrentRTT();
  if (rtt == TimeDelta::PositiveInf() ||
      rtt < kRTTScaling * kMinTailLossProbeDelay) {
    return kMinTailLossProbeDelay;
  }
  return rtt / kRTTScaling;
}

PacketProtocol::Codec* PacketProtocol::PlaintextCodec() {
  class PlaintextCodec final : public Codec {
   public:
    PlaintextCodec() : Codec(Border::Prefix(4)) {}
    StatusOr<Slice> Encode(uint64_t seq_idx, Slice src) const override {
      uint32_t crc =
          crc32(crc32(0, reinterpret_cast<uint8_t*>(&seq_idx), sizeof(seq_idx)),
                src.begin(), src.length());
      OVERNET_TRACE(DEBUG) << "PlaintextCodec Encode: crc=" << crc
                           << " for seq=" << seq_idx << " slice=" << src;
      return src.WithPrefix(
          sizeof(crc), [crc](uint8_t* p) { memcpy(p, &crc, sizeof(crc)); });
    }
    StatusOr<Slice> Decode(uint64_t seq_idx, Slice src) const override {
      if (src.length() < 4) {
        return StatusOr<Slice>(StatusCode::INVALID_ARGUMENT,
                               "Packet too short for crc32");
      }
      uint32_t crc_got;
      memcpy(&crc_got, src.begin(), sizeof(crc_got));
      src.TrimBegin(sizeof(crc_got));
      uint32_t crc_want =
          crc32(crc32(0, reinterpret_cast<uint8_t*>(&seq_idx), sizeof(seq_idx)),
                src.begin(), src.length());
      OVERNET_TRACE(DEBUG) << "PlaintextCodec Decode: got=" << crc_got
                           << " want=" << crc_want << " for seq=" << seq_idx
                           << " slice=" << src;
      if (crc_got != crc_want) {
        return StatusOr<Slice>(StatusCode::INVALID_ARGUMENT,
                               "Packet CRC mismatch");
      }
      return src;
    }
  };
  static PlaintextCodec plaintext_codec;
  return &plaintext_codec;
}

}  // namespace overnet
