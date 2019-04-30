// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/packet_protocol/packet_protocol.h"

#include <iostream>

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
                               uint64_t mss)
    : codec_(codec),
      timer_(timer),
      packet_sender_(packet_sender),
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
  master_ref_.Drop();
}

PacketProtocol::Transaction::Transaction(PacketProtocol* protocol)
    : protocol_(protocol) {
  assert(protocol_->active_transaction_ == nullptr);
  protocol_->active_transaction_ = this;
}

PacketProtocol::Transaction::~Transaction() {
  for (;;) {
    if (quiesce_) {
      assert(protocol_->active_transaction_ == this);
      protocol_->active_transaction_ = nullptr;
      protocol_->Quiesce();
      return;
    }

    if (schedule_send_queue_ && protocol_->state_.has_value()) {
      schedule_send_queue_ = false;
      protocol_->state_->send_queue->Schedule();
      continue;
    }

    assert(protocol_->active_transaction_ == this);
    protocol_->active_transaction_ = nullptr;
    return;
  }
}

void PacketProtocol::Transaction::Send(SendRequestHdl hdl) {
  if (auto* q = send_queue()) {
    schedule_send_queue_ |= q->Add(std::move(hdl));
  }
}

void PacketProtocol::Transaction::QuiesceOnCompletion(Callback<void> callback) {
  OVERNET_TRACE(DEBUG) << "Schedule Quiesce";
  assert(!quiesce_);
  assert(protocol_->quiesce_.empty());
  protocol_->quiesce_ = std::move(callback);
  quiesce_ = true;
}

PacketProtocol::SendQueue* PacketProtocol::Transaction::send_queue() {
  if (quiesce_ || !protocol_->state_.has_value()) {
    return nullptr;
  }
  if (!protocol_->state_->send_queue.has_value()) {
    protocol_->state_->send_queue.Reset(protocol_);
  }
  return protocol_->state_->send_queue.get();
}

///////////////////////////////////////////////////////////////////////////////
// PacketProtocol::Send and friends.
// Defines the send path.

void PacketProtocol::Send(SendRequestHdl send_request) {
  ScopedModule<PacketProtocol> in_pp(this);
  OVERNET_TRACE(DEBUG) << "Send";
  InTransaction([&](Transaction* transaction) {
    transaction->Send(std::move(send_request));
  });
}

bool PacketProtocol::SendQueue::Add(SendRequestHdl hdl) {
  ScopedModule<PacketProtocol> in_pp(protocol_);
  OVERNET_TRACE(DEBUG) << "SendQueue.Add";
  scheduled_tail_loss_probe_.Reset();
  requests_.push(std::move(hdl));
  return !scheduled_;
}

void PacketProtocol::SendQueue::Schedule() {
  ScopedModule<PacketProtocol> in_pp(protocol_);
  OVERNET_TRACE(DEBUG) << "SendQueue.Schedule";
  assert(!scheduled_);
  scheduled_ = true;
  assert(!transmit_request_.has_value());
  transmit_request_.Reset(
      &protocol_->state_->bbr_, [this](const Status& status) {
        ScopedModule<PacketProtocol> scoped_module(protocol_);
        OVERNET_TRACE(DEBUG) << "SendQueue.Schedule --> " << status
                             << " requests=" << requests_.size();
        auto transmit_request = transmit_request_.Take();
        if (status.is_error()) {
          return;
        }
        SendRequestHdl request;
        if (requests_.empty()) {
          last_send_was_tail_loss_probe_ = true;
          protocol_->state_->outstanding_messages.Send(
              std::move(transmit_request), SendRequestHdl(&ack_send_request));
        } else {
          last_send_was_tail_loss_probe_ = false;
          request = std::move(requests_.front());
          requests_.pop();
          protocol_->state_->outstanding_messages.Send(
              std::move(transmit_request), std::move(request));
        }
      });
}

void PacketProtocol::OutstandingMessages::Send(BBR::TransmitRequest bbr_request,
                                               SendRequestHdl request) {
  assert(protocol_->state_);

  const uint64_t seq_idx = send_tip_ + outstanding_.size();
  OVERNET_TRACE(DEBUG) << "OutstandingMessages.Send seq_idx=" << seq_idx;
  SeqNum seq_num(seq_idx, max_outstanding_size_ + 1);

  outstanding_.emplace_back(
      OutstandingPacket{OutstandingPacketState::PENDING,
                        protocol_->state_->received_queue.max_seen_sequence(),
                        Nothing, std::move(request)});

  max_outstanding_size_ =
      std::max(outstanding_.size(), size_t(max_outstanding_size_));

  protocol_->packet_sender_->SendPacket(
      seq_num, PacketSend(protocol_, seq_idx, std::move(bbr_request)));
}

PacketProtocol::PacketSend::PacketSend(PacketProtocol* protocol,
                                       uint64_t seq_idx,
                                       BBR::TransmitRequest bbr_request)
    : protocol_(protocol),
      seq_idx_(seq_idx),
      bbr_request_(std::move(bbr_request)) {
  assert(protocol);
}

Slice PacketProtocol::PacketSend::operator()(LazySliceArgs args) {
  auto protocol = std::move(protocol_);
  auto slice =
      protocol->InTransaction([=, protocol = protocol.get()](Transaction* t) {
        ScopedModule<PacketProtocol> in_pp(protocol);
        if (protocol->state_.has_value()) {
          return protocol->state_->outstanding_messages.GeneratePacket(
              std::move(bbr_request_), seq_idx_, args);
        } else {
          return Slice();
        }
      });
  if (protocol->state_.has_value()) {
    protocol->state_->send_queue->SentMessage();
  }
  return slice;
}

PacketProtocol::PacketSend::~PacketSend() {
  if (protocol_.has_value() && protocol_->state_.has_value()) {
    protocol_->state_->outstanding_messages.CancelPacket(seq_idx_);
  }
}

void PacketProtocol::OutstandingMessages::CancelPacket(uint64_t seq_idx) {
  protocol_->state_->send_queue->SentMessage();
  AckProcessor(this, TimeDelta::Zero()).Nack(seq_idx, Status::Cancelled());
}

Slice PacketProtocol::OutstandingMessages::GeneratePacket(
    BBR::TransmitRequest bbr_request, uint64_t seq_idx, LazySliceArgs args) {
  if (seq_idx < send_tip_) {
    // Frame was nacked before sending (probably due to shutdown).
    OVERNET_TRACE(DEBUG) << "Seq " << seq_idx << " nacked before sending";
    return Slice();
  }

  auto* outstanding_packet = &outstanding_[seq_idx - send_tip_];
  auto send = protocol_->codec_->Encode(
      seq_idx, protocol_->FormatPacket(
                   seq_idx, outstanding_packet->request.borrow(), args));
  if (send.is_error()) {
    OVERNET_TRACE(ERROR) << "Failed to encode packet: " << send.AsStatus();
  }
  // outstanding_ should not have changed
  assert(outstanding_packet == &outstanding_[seq_idx - send_tip_]);
  outstanding_packet->state = OutstandingPacketState::SENT;
  outstanding_packet->bbr_sent_packet =
      bbr_request.Sent(BBR::OutgoingPacket{seq_idx, send->length()});
  ScheduleRetransmit();
  return std::move(*send);
}

Slice PacketProtocol::FormatPacket(uint64_t seq_idx, SendRequest* request,
                                   LazySliceArgs args) {
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

  if (state_->received_queue.CanBuildAck() &&
      (request->must_send_ack() || state_->ack_sender.ShouldSendAck())) {
    auto ack = state_->received_queue.BuildAck(
        seq_idx, timer_->Now(), varint::MaximumLengthWithPrefix(max_length),
        &state_->ack_sender);
    AckFrame::Writer ack_writer(&ack);
    const uint8_t ack_length_length =
        varint::WireSizeFor(ack_writer.wire_length());
    const uint64_t prefix_length = ack_length_length + ack_writer.wire_length();
    return request->GenerateBytes(make_args(prefix_length, true))
        .WithPrefix(prefix_length,
                    [&ack_writer, ack_length_length](uint8_t* p) {
                      ack_writer.Write(varint::Write(ack_writer.wire_length(),
                                                     ack_length_length, p));
                    });
  } else {
    return request->GenerateBytes(make_args(1, false))
        .WithPrefix(1, [](uint8_t* p) { *p = 0; });
  }
}

uint64_t PacketProtocol::ReceivedQueue::max_seen_sequence() const {
  if (received_packets_.empty()) {
    return received_tip_;
  }
  size_t idx = received_packets_.size() - 1;
  while (idx > 0 && received_packets_[idx].state == ReceiveState::UNKNOWN) {
    idx--;
  }
  return received_tip_ + idx;
}

AckFrame PacketProtocol::ReceivedQueue::BuildAck(uint64_t seq_idx,
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

  const auto max_seen = max_seen_sequence();
  assert(max_seen > 0);

  AckFrame ack(max_seen, packet_delay(max_seen));
  for (uint64_t seq_idx = max_seen; seq_idx > received_tip_; seq_idx--) {
    auto& received_packet = received_packets_[seq_idx - received_tip_];
    switch (received_packet.state) {
      case ReceiveState::UNKNOWN:
        OVERNET_TRACE(DEBUG)
            << "Mark unseen packet " << seq_idx << " as NOT_RECEIVED";
        received_packet = ReceivedPacket{ReceiveState::NOT_RECEIVED, now};
        [[fallthrough]];
      case ReceiveState::NOT_RECEIVED:
        ack.AddNack(seq_idx);
        break;
      case ReceiveState::RECEIVED:
        break;
    }
  }

  ack.AdjustForMSS(max_length, packet_delay);

  ack_sender->AckSent(seq_idx, ack.partial());

  OVERNET_TRACE(DEBUG) << "BuildAck generates: " << ack << " bytes="
                       << Slice::FromWriters(AckFrame::Writer(&ack));

  return ack;
}

void PacketProtocol::SendQueue::SentMessage() {
  ScopedModule<PacketProtocol> in_pp(protocol_);
  OVERNET_TRACE(DEBUG) << "SendQueue.SentMessage: requests=" << requests_.size()
                       << " scheduled_tail_loss_probe="
                       << scheduled_tail_loss_probe_.Map(
                              [](const ScheduledTailLossProbe& tlp) {
                                return tlp.when;
                              });
  assert(scheduled_);
  scheduled_ = false;
  protocol_->InTransaction([this](Transaction* t) {
    if (!requests_.empty()) {
      Schedule();
      return;
    }
    if (!last_send_was_tail_loss_probe_ && !t->Closing()) {
      ScheduleTailLossProbe();
      return;
    }
    protocol_->state_->send_queue.Reset();
  });
}

void PacketProtocol::SendQueue::ScheduleTailLossProbe() {
  const auto when = protocol_->timer_->Now() + protocol_->TailLossProbeDelay();
  OVERNET_TRACE(DEBUG) << "SendQueue.ScheduleTailLossProbe: requests="
                       << requests_.size() << " scheduled_tail_loss_probe="
                       << scheduled_tail_loss_probe_.Map(
                              [](const ScheduledTailLossProbe& tlp) {
                                return tlp.when;
                              })
                       << " when=" << when;
  if (!requests_.empty()) {
    return;
  }
  if (scheduled_tail_loss_probe_.has_value() &&
      scheduled_tail_loss_probe_->when <= when) {
    return;
  }
  scheduled_tail_loss_probe_.Reset(
      protocol_->timer_, when, [this](const Status& status) {
        ScopedModule<PacketProtocol> in_pp(protocol_);
        OVERNET_TRACE(DEBUG) << "SendQueue.ScheduleTailLossProbe --> " << status
                             << " requests=" << requests_.size();
        if (status.is_error()) {
          return;
        }
        scheduled_tail_loss_probe_.Reset();
        ForceSendAck();
      });
}

void PacketProtocol::SendQueue::ForceSendAck() {
  if (!scheduled_) {
    protocol_->InTransaction([](Transaction* t) { t->ScheduleForcedAck(); });
  }
}

///////////////////////////////////////////////////////////////////////////////
// PacketProtocol::Process and friends.
// Defines the receive path.

void PacketProtocol::Process(TimeStamp received, SeqNum seq_num, Slice slice,
                             ProcessCallback handle_message) {
  ScopedModule<PacketProtocol> scoped_module(this);
  Transaction transaction(this);

  OVERNET_TRACE(DEBUG) << "Process: " << slice;

  if (!state_.has_value()) {
    return;
  }

  state_->ack_sender.NeedAck(
      state_->received_queue.Received(seq_num, received, [&](uint64_t seq_idx) {
        return ProcessMessage(seq_idx, std::move(slice), received,
                              std::move(handle_message));
      }));
  state_->outstanding_messages.ReceivedPacket();
}

template <class F>
PacketProtocol::AckUrgency PacketProtocol::ReceivedQueue::Received(
    SeqNum seq_num, TimeStamp received, F logic) {
  const auto seq_idx =
      seq_num.Reconstruct(received_tip_ + received_packets_.size());

  OVERNET_TRACE(DEBUG) << "Process seq:" << seq_idx;

  if (seq_idx < received_tip_) {
    return AckUrgency::NOT_REQUIRED;
  }

  if (!EnsureValidReceivedPacket(seq_idx, received)) {
    return AckUrgency::NOT_REQUIRED;
  }

  auto* received_packet = &received_packets_[seq_idx - received_tip_];
  if (received_packet->state != ReceiveState::UNKNOWN) {
    OVERNET_TRACE(DEBUG) << "frozen as " << received_packet->state;
    return AckUrgency::NOT_REQUIRED;
  }

  auto pmr = logic(seq_idx);
  OVERNET_TRACE(DEBUG) << "Process seq:" << seq_idx
                       << " process_message_result=" << pmr << " is_last="
                       << (seq_idx ==
                           received_tip_ + received_packets_.size() - 1)
                       << " optional_ack_run_length="
                       << optional_ack_run_length_
                       << " received_packets=" << ReceivedPacketSummary();

  // received_packet may not be valid anymore, but the packet *must* exist
  assert(seq_idx >= received_tip_);
  received_packet = &received_packets_[seq_idx - received_tip_];

  switch (pmr) {
    case ProcessMessageResult::NOT_PROCESSED:
      // Failed processing packets shouldn't generate traffic.
      return AckUrgency::NOT_REQUIRED;
    case ProcessMessageResult::NACK:
      optional_ack_run_length_ = 0;
      *received_packet = ReceivedPacket{ReceiveState::NOT_RECEIVED, received};
      // Always send a nack as soon as we realize one is necessary.
      return AckUrgency::SEND_IMMEDIATELY;
    case ProcessMessageResult::OPTIONAL_ACK:
      // If we get a single ack without a payload, we suppress sending a reply
      // ack.
      optional_ack_run_length_++;
      if (optional_ack_run_length_ < 5) {
        *received_packet = ReceivedPacket{ReceiveState::RECEIVED, received};
        return AckUrgency::NOT_REQUIRED;
      }
      [[fallthrough]];
    case ProcessMessageResult::ACK: {
      optional_ack_run_length_ = 0;
      *received_packet = ReceivedPacket{ReceiveState::RECEIVED, received};
      int num_received = 0;
      for (const auto& pkt : received_packets_) {
        switch (pkt.state) {
          case ReceiveState::RECEIVED:
            num_received++;
            if (num_received >= 3) {
              return AckUrgency::SEND_IMMEDIATELY;
            }
            break;
          default:
            break;
        }
      }
      // Got some data, make sure there's an ack scheduled soon.
      return AckUrgency::SEND_SOON;
    }
    case ProcessMessageResult::ACK_URGENTLY: {
      optional_ack_run_length_ = 0;
      *received_packet = ReceivedPacket{ReceiveState::RECEIVED, received};
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
    uint64_t seq_idx, Slice slice, TimeStamp received,
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
    state_->outstanding_messages.ProcessValidAck(ack.Take(), received);
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
    if (pkt.state == OutstandingPacketState::ACKED) {
      // Previously acked packet becomes nacked: this is an error.
      return Status(StatusCode::INVALID_ARGUMENT,
                    "Previously acked packet becomes nacked");
    }
  }
  for (size_t i = 0; i < ack.ack_to_seq() - send_tip_; i++) {
    if (!outstanding_[i].bbr_sent_packet.has_value()) {
      return Status(StatusCode::INVALID_ARGUMENT, "Ack/nack unsent sequence");
    }
  }
  return Status::Ok();
}

void PacketProtocol::OutstandingMessages::ProcessValidAck(AckFrame ack,
                                                          TimeStamp received) {
  // Basic validation. Can assert anything that should be an error because
  // ValidateAck should have been called prior.
  if (ack.ack_to_seq() < send_tip_) {
    return;
  }
  assert(ack.ack_to_seq() < send_tip_ + outstanding_.size());

  // Move receive window forward.
  const auto max_seen_sequence_at_send =
      outstanding_[ack.ack_to_seq() - send_tip_].max_seen_sequence_at_send;
  if (max_seen_sequence_at_send > 0) {
    protocol_->state_->received_queue.SetTip(max_seen_sequence_at_send,
                                             received);
  }

  AckProcessor ack_processor(this,
                             TimeDelta::FromMicroseconds(ack.ack_delay_us()));

  // Fail any nacked packets.
  // Iteration is from oldest packet to newest, such that the OLDEST nacked
  // message is the most likely to be sent first. This has the important
  // consequence that if the packet was a fragment of a large message that was
  // rejected due to buffering, the earlier pieces (that are more likely to fit)
  // are retransmitted first.
  for (auto nack_seq : ack.nack_seqs()) {
    ack_processor.Nack(nack_seq, Status::Unavailable());
  }

  // Clear out outstanding packet references, propagating acks.
  for (size_t i = send_tip_; i <= ack.ack_to_seq(); i++) {
    OutstandingPacket& pkt = outstanding_[i - send_tip_];
    if (!pkt.request.empty()) {
      ack_processor.Ack(i);
    }
  }
}

void PacketProtocol::OutstandingMessages::AckProcessor::Ack(uint64_t ack_seq) {
  OutstandingPacket& pkt =
      outstanding_->outstanding_[ack_seq - outstanding_->send_tip_];
  auto request = std::move(pkt.request);
  if (!request.empty()) {
    assert(pkt.state == OutstandingPacketState::SENT);
    pkt.state = OutstandingPacketState::ACKED;
    bbr_ack_.acked_packets.push_back(*pkt.bbr_sent_packet);
    request.Ack(Status::Ok());
  }
}

void PacketProtocol::OutstandingMessages::AckProcessor::Nack(
    uint64_t nack_seq, const Status& status) {
  OVERNET_TRACE(DEBUG) << "AckProcessor.Nack: seq=" << nack_seq
                       << " status=" << status
                       << " send_tip=" << outstanding_->send_tip_;
  assert(status.is_error());
  if (outstanding_->protocol_->state_.has_value()) {
    outstanding_->protocol_->state_->ack_sender.OnNack(nack_seq);
  }
  if (nack_seq < outstanding_->send_tip_) {
    return;
  }
  OutstandingPacket& pkt =
      outstanding_->outstanding_[nack_seq - outstanding_->send_tip_];
  auto request = std::move(pkt.request);
  OVERNET_TRACE(DEBUG) << "AckProcessor.Nack: seq=" << nack_seq
                       << " has_request=" << !request.empty()
                       << " state=" << pkt.state;
  if (request.empty()) {
    return;
  }
  switch (pkt.state) {
    case OutstandingPacketState::PENDING:
      pkt.state = OutstandingPacketState::NACKED;
      break;
    case OutstandingPacketState::SENT:
      assert(pkt.bbr_sent_packet.has_value());
      assert(pkt.bbr_sent_packet->outgoing.sequence == nack_seq);
      bbr_ack_.nacked_packets.push_back(*pkt.bbr_sent_packet);
      pkt.state = OutstandingPacketState::NACKED;
      break;
    case OutstandingPacketState::NACKED:
      break;
    default:
      // Previously acked packet becomes nacked: this is an error.
      abort();
  }
  request.Ack(status);
}

PacketProtocol::OutstandingMessages::AckProcessor::~AckProcessor() {
  bool empty = true;
  // Offset send_time to account for queuing delay on peer.
  for (auto& pkt : bbr_ack_.acked_packets) {
    empty = false;
    pkt.send_time = pkt.send_time + queue_delay_;
  }
  for (auto& pkt : bbr_ack_.nacked_packets) {
    empty = false;
    pkt.send_time = pkt.send_time + queue_delay_;
  }
  if (!empty && outstanding_->protocol_->state_.has_value()) {
    outstanding_->protocol_->state_->bbr_.OnAck(bbr_ack_);
  }
  while (!outstanding_->outstanding_.empty() &&
         outstanding_->outstanding_.front().request.empty()) {
    outstanding_->send_tip_++;
    outstanding_->outstanding_.pop_front();
  }
}

void PacketProtocol::ReceivedQueue::SetTip(uint64_t seq_idx,
                                           TimeStamp received) {
  assert(seq_idx >= 1);
  uint64_t tip_idx = seq_idx - 1;
  if (tip_idx <= received_tip_) {
    return;
  }
  if (!EnsureValidReceivedPacket(tip_idx, received)) {
    abort();
  }
  received_packets_.erase(
      received_packets_.begin(),
      received_packets_.begin() + (tip_idx - received_tip_));
  received_tip_ = tip_idx;
}

///////////////////////////////////////////////////////////////////////////////
// Ack scheduling

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

void PacketProtocol::AckSender::NeedAck(AckUrgency urgency) {
  if (urgency <= urgency_) {
    return;
  }

  OVERNET_TRACE(DEBUG) << "AckSender.NeedAck"
                       << " urgency=" << urgency
                       << " all_acks_acknowledged=" << all_acks_acknowledged_
                       << " sent_full_acks=" << SentFullAcksString();
  urgency_ = urgency;
  sent_full_acks_.clear();
  all_acks_acknowledged_ = false;
  assert(protocol_->state_.has_value());
  if (!protocol_->state_->send_queue.has_value()) {
    protocol_->state_->send_queue.Reset(protocol_);
  }
  switch (urgency_) {
    case AckUrgency::NOT_REQUIRED:
      abort();
    case AckUrgency::SEND_SOON:
      protocol_->state_->send_queue->ScheduleTailLossProbe();
      break;
    case AckUrgency::SEND_IMMEDIATELY:
      protocol_->state_->send_queue->ForceSendAck();
      break;
  }
}

void PacketProtocol::AckSender::AckSent(uint64_t seq_idx, bool partial) {
  OVERNET_TRACE(DEBUG) << "AckSender.AckSent seq_idx=" << seq_idx
                       << " partial=" << partial
                       << " all_acks_acknowledged=" << all_acks_acknowledged_
                       << " sent_full_acks=" << SentFullAcksString();

  if (!sent_full_acks_.empty()) {
    assert(seq_idx > sent_full_acks_.back());
  }
  urgency_ = AckUrgency::NOT_REQUIRED;
  if (!partial) {
    sent_full_acks_.push_back(seq_idx);
  } else if (sent_full_acks_.empty()) {
    NeedAck(AckUrgency::SEND_SOON);
  }
}

void PacketProtocol::AckSender::OnNack(uint64_t seq) {
  OVERNET_TRACE(DEBUG) << "AckSender.OnNack"
                       << " sent_full_acks=" << SentFullAcksString()
                       << " seq=" << seq
                       << " all_acks_acknowledged=" << all_acks_acknowledged_;
  auto it =
      std::lower_bound(sent_full_acks_.begin(), sent_full_acks_.end(), seq);
  if (it == sent_full_acks_.end() || *it != seq) {
    return;
  }
  sent_full_acks_.erase(it);
  if (sent_full_acks_.empty()) {
    NeedAck(AckUrgency::SEND_SOON);
  }
}

void PacketProtocol::AckSender::OnAck(uint64_t seq) {
  OVERNET_TRACE(DEBUG) << "AckSender.OnAck"
                       << " sent_full_acks=" << SentFullAcksString()
                       << " seq=" << seq
                       << " all_acks_acknowledged=" << all_acks_acknowledged_;
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
                       << rto_timer_.has_value()
                       << " deadline=" << RetransmitDeadline();
  if (rto_timer_.has_value()) {
    return;
  }
  if (auto timeout = RetransmitDeadline(); timeout.has_value()) {
    rto_timer_.Reset(
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
              protocol->state_->outstanding_messages.NackAll();
              return;
            }
            protocol->state_->outstanding_messages.CheckRetransmit();
          });
        });
  }
}

Optional<TimeStamp> PacketProtocol::OutstandingMessages::RetransmitDeadline() {
  for (const auto& outstanding : outstanding_) {
    if (outstanding.bbr_sent_packet.has_value() &&
        outstanding.state == OutstandingPacketState::SENT) {
      return outstanding.bbr_sent_packet->send_time +
             protocol_->RetransmitDelay();
    }
  }
  return Nothing;
}

void PacketProtocol::OutstandingMessages::CheckRetransmit() {
  if (!protocol_->state_.has_value()) {
    return;
  }
  rto_timer_.Reset();
  const auto nack_before =
      protocol_->timer_->Now() - protocol_->RetransmitDelay();
  OVERNET_TRACE(DEBUG) << "OutstandingMessages.CheckRetransmit: nack_before="
                       << nack_before
                       << " (current_rtt=" << protocol_->CurrentRTT() << ")";
  AckProcessor ack_processor(this, TimeDelta::Zero());
  for (size_t i = 0; i < outstanding_.size(); i++) {
    if (!outstanding_[i].bbr_sent_packet.has_value()) {
      OVERNET_TRACE(DEBUG) << "OutstandingMessages.CheckRetransmit: seq "
                           << (send_tip_ + i) << " not sent: STOP";
      break;
    }
    if (outstanding_[i].bbr_sent_packet->send_time > nack_before) {
      OVERNET_TRACE(DEBUG) << "OutstandingMessages.CheckRetransmit: seq "
                           << (send_tip_ + i) << " sent at "
                           << outstanding_[i].bbr_sent_packet->send_time
                           << ": STOP";
      break;
    }
    OVERNET_TRACE(DEBUG) << "OutstandingMessages.CheckRetransmit: seq "
                         << (send_tip_ + i) << " sent at "
                         << outstanding_[i].bbr_sent_packet->send_time
                         << ": NACK";
    ack_processor.Nack(send_tip_ + i, Status::Unavailable());
  }
  ScheduleRetransmit();
}

void PacketProtocol::OutstandingMessages::NackAll() {
  OVERNET_TRACE(DEBUG) << "OutstandingMessages.NackAll";
  AckProcessor ack_processor(this, TimeDelta::Zero());
  for (uint64_t i = send_tip_, end = send_tip_ + outstanding_.size(); i < end;
       i++) {
    if (outstanding_[i - send_tip_].request.empty()) {
      continue;
    }
    ack_processor.Nack(i, Status::Cancelled());
  }
}

///////////////////////////////////////////////////////////////////////////////
// Utilities

TimeDelta PacketProtocol::CurrentRTT() const {
  return std::max(TimeDelta::FromMilliseconds(1), state_->bbr_.rtt());
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

PacketProtocol::Codec* PacketProtocol::NullCodec() {
  class NullCodec final : public Codec {
   public:
    NullCodec() : Codec(Border::None()) {}
    StatusOr<Slice> Encode(uint64_t seq_idx, Slice src) const override {
      return src;
    }
    StatusOr<Slice> Decode(uint64_t seq_idx, Slice src) const override {
      return src;
    }
  };
  static NullCodec null_codec;
  return &null_codec;
}

}  // namespace overnet
