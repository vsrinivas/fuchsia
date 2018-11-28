// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "packet_protocol.h"
#include <iostream>

namespace overnet {

static const char kClosing[] = "Closing";
static const char kMaybeScheduleAck[] = "MaybeScheduleAck";
static const char kRequestSendAck[] = "RequestSendAck";
static const char kRequestTransmit[] = "RequestTransmit";
static const char kScheduleRTO[] = "ScheduleRTO";
static const char kStartNext[] = "StartNext";
static const char kTransmitPacket[] = "TransmitPacket";

const char PacketProtocol::kProcessedPacket[] = "ProcessedPacket";

template <class T>
void Drain(T* container) {
  T clean;
  container->swap(clean);
  clean.clear();
}

void PacketProtocol::Close(Callback<void> quiesced) {
  ScopedModule<PacketProtocol> scoped_module(this);
  assert(state_ == State::READY);
  OVERNET_TRACE(DEBUG) << "Close outstanding_ops=" << outstanding_ops_;
  OutstandingOp<kClosing> op(this);
  state_ = State::CLOSING;
  quiesced_ = std::move(quiesced);
  // Stop waiting for things.
  rto_scheduler_.Reset();
  ack_scheduler_.Reset();
  outgoing_bbr_.CancelRequestTransmit();
  NackBefore(TimeStamp::AfterEpoch(TimeDelta::PositiveInf()),
             Status::Cancelled());
  send_tip_ = std::numeric_limits<decltype(send_tip_)>::max();
  Drain(&outstanding_);
  Drain(&queued_);
}

void PacketProtocol::RequestSendAck() {
  // Prevent quiescing during ack generation (otherwise cancelling a scheduled
  // ack might cause us to quiesce).
  OutstandingOp<kRequestSendAck> op(this);
  ScopedModule<PacketProtocol> scoped_module(this);
  MaybeForceAck();
}

void PacketProtocol::Send(SendRequestHdl send_request) {
  ScopedModule<PacketProtocol> scoped_module(this);
  OVERNET_TRACE(DEBUG) << "Send state=" << static_cast<int>(state_)
                       << " qsize=" << queued_.size()
                       << " outstanding=" << outstanding_.size()
                       << " sending=" << sending_.has_value();
  if (state_ != State::READY) {
    // Discard result, forcing callbacks to be made
    return;
  }
  MaybeSendSlice(QueuedPacket{ScopedOp::current(), std::move(send_request)});
}

void PacketProtocol::MaybeSendSlice(QueuedPacket&& packet) {
  OVERNET_TRACE(DEBUG) << "MaybeSendSlice: queued=" << queued_.size()
                       << " sending=" << sending_.has_value()
                       << " transmitting=" << transmitting_;
  if (!queued_.empty() || sending_ || transmitting_) {
    queued_.emplace_back(std::forward<QueuedPacket>(packet));
    return;
  }
  SendSlice(std::forward<QueuedPacket>(packet));
}

void PacketProtocol::SendSlice(QueuedPacket&& packet) {
  assert(!transmitting_);
  assert(!sending_);
  sending_.Reset(std::forward<QueuedPacket>(packet));
  assert(!sending_->request.empty());
  OVERNET_TRACE(DEBUG) << "SendSlice send_tip=" << send_tip_
                       << " outstanding=" << outstanding_.size();
  outgoing_bbr_.RequestTransmit([self = OutstandingOp<kRequestTransmit>(this)](
                                    const Status& status) mutable {
    ScopedModule<PacketProtocol> scoped_module(self.get());
    if (status.is_error()) {
      self->sending_->request.Ack(status);
      self->sending_.Reset();
      return;
    }
    self->TransmitPacket();
  });
}

void PacketProtocol::TransmitPacket() {
  ScopedOp op(sending_->op);
  assert(!transmitting_);
  const uint64_t seq_idx = send_tip_ + outstanding_.size();
  if (seq_idx - send_tip_ > max_outstanding_size_) {
    max_outstanding_size_ = seq_idx - send_tip_;
  }
  SeqNum seq_num(seq_idx, max_outstanding_size_);
  OVERNET_TRACE(DEBUG) << "TransmitPacket seq=" << seq_idx << " -> " << seq_num
                       << " (send_tip=" << send_tip_ << ")"
                       << " outstanding=" << outstanding_.size()
                       << " rto_scheduler?=" << (rto_scheduler_ ? "YES" : "NO");
  if (outstanding_.empty() || !rto_scheduler_) {
    KeepAlive();
  }
  outstanding_.emplace_back(OutstandingPacket{
      timer_->Now(), OutstandingPacketState::PENDING, false, false, max_seen_,
      Nothing, std::move(sending_.Take().request)});
  assert(!sending_.has_value());
  transmitting_ = true;
  packet_sender_->SendPacket(
      seq_num,
      [request = outstanding_.back().request.borrow(), op = ScopedOp::current(),
       self = OutstandingOp<kTransmitPacket>(this),
       seq_idx](LazySliceArgs args) {
        ScopedModule<PacketProtocol> scoped_module(self.get());
        ScopedOp scoped_op(op);
        auto outstanding_packet = [&]() -> OutstandingPacket* {
          if (seq_idx < self->send_tip_) {
            // Frame was nacked before sending (probably due to shutdown).
            OVERNET_TRACE(DEBUG)
                << "Seq " << seq_idx << " nacked before sending";
            return nullptr;
          }
          const auto outstanding_idx = seq_idx - self->send_tip_;
          OVERNET_TRACE(DEBUG)
              << "GeneratePacket seq=" << seq_idx
              << " send_tip=" << self->send_tip_
              << " outstanding_idx=" << outstanding_idx
              << " outstanding_size=" << self->outstanding_.size();
          assert(outstanding_idx < self->outstanding_.size());
          return &self->outstanding_[outstanding_idx];
        };
        if (auto* pkt = outstanding_packet()) {
          if (pkt->request.empty()) {
            return Slice();
          }
        } else {
          return Slice();
        }
        auto gen_pkt = self->GeneratePacket(request, args);
        auto encoded_payload =
            self->codec_->Encode(seq_idx, std::move(gen_pkt.payload));
        if (encoded_payload.is_error()) {
          OVERNET_TRACE(ERROR)
              << "Failed to encode packet: " << encoded_payload.AsStatus();
        }
        if (auto* pkt = outstanding_packet()) {
          assert(!pkt->bbr_sent_packet.has_value());
          assert(pkt->state == OutstandingPacketState::PENDING);
          pkt->state = OutstandingPacketState::SENT;
          pkt->has_ack = gen_pkt.has_ack;
          pkt->is_pure_ack = gen_pkt.is_pure_ack;
          pkt->bbr_sent_packet = self->outgoing_bbr_.ScheduleTransmit(
              BBR::OutgoingPacket{seq_idx, encoded_payload->length()});
        } else {
          return Slice();
        }
        if (gen_pkt.has_ack) {
          self->last_sent_ack_ = seq_idx;
        }
        return std::move(*encoded_payload);
      },
      [self = OutstandingOp<kStartNext>(this)]() {
        self->transmitting_ = false;
        self->ContinueSending();
      });
}

PacketProtocol::GeneratedPacket PacketProtocol::GeneratePacket(
    SendRequest* request, LazySliceArgs args) {
  auto ack =
      GenerateAck(std::min(static_cast<uint64_t>(args.max_length), mss_));
  auto make_args = [=](uint64_t prefix_length, bool has_other_content) {
    auto max_length = std::min(static_cast<uint64_t>(args.max_length), mss_);
    auto subtract_from_max =
        std::min(max_length,
                 prefix_length + codec_->border.prefix + codec_->border.suffix);
    max_length -= subtract_from_max;
    return LazySliceArgs{args.desired_border.WithAddedPrefix(
                             prefix_length + codec_->border.prefix),
                         max_length,
                         has_other_content || args.has_other_content};
  };
  if (ack) {
    sent_ack_ = true;
    AckFrame::Writer ack_writer(ack.get());
    const uint8_t ack_length_length =
        varint::WireSizeFor(ack_writer.wire_length());
    const uint64_t prefix_length = ack_length_length + ack_writer.wire_length();
    auto payload_slice = request->GenerateBytes(make_args(prefix_length, true));
    return GeneratedPacket{
        payload_slice.WithPrefix(prefix_length,
                                 [&ack_writer, ack_length_length](uint8_t* p) {
                                   ack_writer.Write(
                                       varint::Write(ack_writer.wire_length(),
                                                     ack_length_length, p));
                                 }),
        true, payload_slice.length() == 0};
  } else {
    auto payload_slice = request->GenerateBytes(make_args(1, false));
    return GeneratedPacket{
        payload_slice.WithPrefix(1, [](uint8_t* p) { *p = 0; }), false, false};
  }
}

StatusOr<PacketProtocol::AckActions> PacketProtocol::HandleAck(
    const AckFrame& ack, bool is_synthetic) {
  OVERNET_TRACE(DEBUG) << "HandleAck: " << ack << " synthetic=" << is_synthetic;

  // TODO(ctiller): inline vectors to avoid allocations.
  AckActions actions;

  // Validate ack, and ignore if it's old.
  if (ack.ack_to_seq() < send_tip_) {
    return actions;
  }
  if (ack.ack_to_seq() >= send_tip_ + outstanding_.size()) {
    return Status(StatusCode::INVALID_ARGUMENT,
                  "Ack packet past sending sequence");
  }
  BBR::Ack bbr_ack;
  // Move receive window forward.
  auto new_recv_tip = outstanding_[ack.ack_to_seq() - send_tip_].ack_to_seq;
  if (new_recv_tip != recv_tip_) {
    assert(new_recv_tip > recv_tip_);
    while (!received_packets_.empty()) {
      auto it = received_packets_.begin();
      if (it->first >= new_recv_tip) {
        break;
      }
      received_packets_.erase(it);
    }
    recv_tip_ = new_recv_tip;
  }
  // Fail any nacked packets.
  // Nacks are received in descending order of sequence number. We iterate the
  // callbacks here in reverse order then so that the OLDEST nacked message is
  // the most likely to be sent first. This has the important consequence that
  // if the packet was a fragment of a large message that was rejected due to
  // buffering, the earlier pieces (that are more likely to fit) are
  // retransmitted first.
  bool nacked_last_ack = false;
  for (auto it = ack.nack_seqs().rbegin(); it != ack.nack_seqs().rend(); ++it) {
    auto nack_seq = *it;
    if (nack_seq < send_tip_) {
      continue;
    }
    if (nack_seq >= send_tip_ + outstanding_.size()) {
      return Status(StatusCode::INVALID_ARGUMENT, "Nack past sending sequence");
    }
    OutstandingPacket& pkt = outstanding_[nack_seq - send_tip_];
    auto request = std::move(pkt.request);
    if (!request.empty()) {
      if (!pkt.bbr_sent_packet.has_value()) {
        if (is_synthetic) {
          OVERNET_TRACE(DEBUG)
              << "Nack unsent packet: fake out bbr scheduling for seq "
              << nack_seq;
          pkt.bbr_sent_packet =
              outgoing_bbr_.ScheduleTransmit(BBR::OutgoingPacket{nack_seq, 0});
        } else {
          return Status(StatusCode::INVALID_ARGUMENT, "Nack unsent sequence");
        }
      }
      assert(pkt.bbr_sent_packet->outgoing.sequence == nack_seq);
      OVERNET_TRACE(DEBUG) << "NACK: " << nack_seq
                           << " size=" << pkt.bbr_sent_packet->outgoing.size
                           << " send_time=" << pkt.bbr_sent_packet->send_time;
      actions.nacks.emplace_back(std::move(request));
      bbr_ack.nacked_packets.push_back(*pkt.bbr_sent_packet);
      if (pkt.has_ack && nack_seq == last_sent_ack_) {
        nacked_last_ack = true;
      }
      switch (pkt.state) {
        case OutstandingPacketState::PENDING:
        case OutstandingPacketState::SENT:
          pkt.state = OutstandingPacketState::NACKED;
          break;
        case OutstandingPacketState::NACKED:
          break;
        case OutstandingPacketState::ACKED:
          // Previously acked packet becomes nacked: this is an error.
          return Status(StatusCode::INVALID_ARGUMENT,
                        "Previously acked packet becomes nacked");
      }
    }
  }
  // Clear out outstanding packet references, propagating acks.
  while (send_tip_ <= ack.ack_to_seq()) {
    OutstandingPacket& pkt = outstanding_.front();
    auto request = std::move(pkt.request);
    if (!pkt.bbr_sent_packet.has_value()) {
      return Status(StatusCode::INVALID_ARGUMENT, "Ack unsent sequence");
    }
    assert(pkt.bbr_sent_packet->outgoing.sequence == send_tip_);
    send_tip_++;
    if (!request.empty()) {
      OVERNET_TRACE(DEBUG) << "ACK: " << pkt.bbr_sent_packet->outgoing.sequence
                           << " size=" << pkt.bbr_sent_packet->outgoing.size
                           << " send_time=" << pkt.bbr_sent_packet->send_time;
      switch (pkt.state) {
        case OutstandingPacketState::PENDING:
        case OutstandingPacketState::NACKED:
        case OutstandingPacketState::ACKED:
          abort();
        case OutstandingPacketState::SENT:
          pkt.state = OutstandingPacketState::ACKED;
          break;
      }
      bbr_ack.acked_packets.push_back(*pkt.bbr_sent_packet);
      outstanding_.pop_front();
      actions.acks.emplace_back(std::move(request));
    } else {
      outstanding_.pop_front();
    }
  }
  const auto delay = TimeDelta::FromMicroseconds(ack.ack_delay_us());
  // Offset send_time to account for queuing delay on peer.
  for (auto& pkt : bbr_ack.acked_packets) {
    pkt.send_time = pkt.send_time + delay;
  }
  for (auto& pkt : bbr_ack.nacked_packets) {
    pkt.send_time = pkt.send_time + delay;
  }
  if (nacked_last_ack) {
    MaybeSendAck();
  }

  actions.bbr_ack = std::move(bbr_ack);

  return actions;
}

void PacketProtocol::ContinueSending() {
  ScopedModule<PacketProtocol> in_pp(this);
  OVERNET_TRACE(DEBUG) << "ContinueSending: queued=" << queued_.size()
                       << " sending=" << sending_.has_value()
                       << " transmitting=" << transmitting_
                       << " state=" << static_cast<int>(state_);
  if (!queued_.empty() && !sending_ && !transmitting_ &&
      state_ == State::READY) {
    QueuedPacket p = std::move(queued_.front());
    queued_.pop_front();
    SendSlice(std::move(p));
  } else if (ack_after_sending_) {
    ack_after_sending_ = false;
    MaybeSendAck();
  }
}

PacketProtocol::ProcessedPacket PacketProtocol::Process(TimeStamp received,
                                                        SeqNum seq_num,
                                                        Slice slice) {
  ScopedModule<PacketProtocol> scoped_module(this);
  using StatusType = StatusOr<Optional<Slice>>;
  OutstandingOp<kProcessedPacket> op(this);

  OVERNET_TRACE(DEBUG) << "Process: " << slice;

  // Validate sequence number, ignore if it's old.
  const auto seq_idx = seq_num.Reconstruct(recv_tip_);
  OVERNET_TRACE(DEBUG) << "Receive sequence " << seq_num << "=" << seq_idx
                       << " recv_tip " << recv_tip_
                       << " max_seen=" << max_seen_;
  if (state_ == State::CLOSED) {
    return ProcessedPacket(op, seq_idx, ProcessedPacket::SendAck::NONE,
                           ReceiveState::UNKNOWN, Nothing, Nothing);
  }
  if (seq_idx < recv_tip_) {
    return ProcessedPacket(op, seq_idx, ProcessedPacket::SendAck::NONE,
                           ReceiveState::UNKNOWN, Nothing, Nothing);
  }

  // Keep track of the biggest valid sequence we've seen.
  if (seq_idx > max_seen_) {
    OVERNET_TRACE(DEBUG) << "new max_seen";
    max_seen_ = seq_idx;
  }

  KeepAlive();

  auto slice_status = codec_->Decode(seq_idx, std::move(slice));
  if (slice_status.is_error()) {
    OVERNET_TRACE(ERROR) << "Failed to decode packet: "
                         << slice_status.AsStatus();
    return ProcessedPacket(op, seq_idx, ProcessedPacket::SendAck::NONE,
                           ReceiveState::UNKNOWN, Nothing, Nothing);
  }
  slice = std::move(*slice_status);

  const uint8_t* p = slice.begin();
  const uint8_t* end = slice.end();

  if (p == end) {
    return ProcessedPacket(op, seq_idx, ProcessedPacket::SendAck::NONE,
                           ReceiveState::UNKNOWN, Nothing, Nothing);
  }

  uint64_t ack_length;
  if (!varint::Read(&p, end, &ack_length)) {
    return ProcessedPacket(
        op, seq_idx, ProcessedPacket::SendAck::NONE, ReceiveState::UNKNOWN,
        StatusType(StatusCode::INVALID_ARGUMENT,
                   "Failed to parse ack length from message"),
        Nothing);
  }
  slice.TrimBegin(p - slice.begin());

  OVERNET_TRACE(DEBUG) << "ack_length=" << ack_length;

  if (ack_length > slice.length()) {
    return ProcessedPacket(
        op, seq_idx, ProcessedPacket::SendAck::NONE, ReceiveState::UNKNOWN,
        StatusType(StatusCode::INVALID_ARGUMENT,
                   "Ack frame claimed to be past end of message"),
        Nothing);
  }

  ProcessedPacket::SendAck ack = ProcessedPacket::SendAck::NONE;

  auto it = received_packets_.lower_bound(seq_idx);
  if (it == received_packets_.end() || it->first != seq_idx) {
    it = received_packets_.insert(
        it, std::make_pair(seq_idx,
                           ReceivedPacket{ReceiveState::UNKNOWN, received}));
  } else {
    OVERNET_TRACE(DEBUG) << "frozen as " << static_cast<int>(it->second.state);
    return ProcessedPacket(op, seq_idx, ProcessedPacket::SendAck::NONE,
                           ReceiveState::UNKNOWN, Nothing, Nothing);
  }

  const bool is_pure_ack = ack_length > 0 && ack_length == slice.length();
  bool suppress_ack = is_pure_ack;
  bool prev_was_also_suppressed = false;
  bool prev_was_discontiguous = false;
  if (suppress_ack && it != received_packets_.begin()) {
    auto prev = std::prev(it);
    if (prev->first != it->first - 1) {
      suppress_ack = false;
      prev_was_discontiguous = true;
    } else {
      switch (prev->second.state) {
        case ReceiveState::UNKNOWN:
          assert(false);
          break;
        case ReceiveState::NOT_RECEIVED:
        case ReceiveState::RECEIVED:
          break;
        case ReceiveState::RECEIVED_AND_SUPPRESSED_ACK:
          suppress_ack = false;
          prev_was_also_suppressed = true;
          break;
      }
    }
  }
  if (suppress_ack && it->first != received_packets_.rbegin()->first) {
    suppress_ack = false;
  }
  const auto final_receive_state =
      suppress_ack ? ReceiveState::RECEIVED_AND_SUPPRESSED_ACK
                   : ReceiveState::RECEIVED;

  OVERNET_TRACE(DEBUG) << "pure_ack=" << is_pure_ack
                       << " suppress_ack=" << suppress_ack << " is_last="
                       << (it->first == received_packets_.rbegin()->first)
                       << " prev_was_also_suppressed="
                       << prev_was_also_suppressed
                       << " prev_was_discontiguous=" << prev_was_discontiguous;

  if (suppress_ack) {
    OVERNET_TRACE(DEBUG) << "ack suppressed";
  } else {
    if (seq_idx >= kMaxUnackedReceives &&
        max_acked_ <= seq_idx - kMaxUnackedReceives) {
      OVERNET_TRACE(DEBUG) << "Select force ack";
      ack = ProcessedPacket::SendAck::FORCE;
    } else if (!sent_ack_) {
      OVERNET_TRACE(DEBUG) << "Select force first ack";
      ack = ProcessedPacket::SendAck::FORCE;
      sent_ack_ = true;
    } else {
      OVERNET_TRACE(DEBUG) << "Select schedule ack";
      ack = ProcessedPacket::SendAck::SCHEDULE;
    }
  }

  if (ack_length == 0) {
    return ProcessedPacket(op, seq_idx, ack, final_receive_state, slice,
                           Nothing);
  }

  auto ack_action_status = AckFrame::Parse(slice.TakeUntilOffset(ack_length))
                               .Then([this](const AckFrame& ack_frame) {
                                 return HandleAck(ack_frame, false);
                               });
  if (ack_action_status.is_error()) {
    return ProcessedPacket(op, seq_idx, ack, final_receive_state,
                           ack_action_status.AsStatus(), Nothing);
  }

  return ProcessedPacket(op, seq_idx, ack, final_receive_state, slice,
                         std::move(*ack_action_status));
}

void PacketProtocol::ProcessedPacket::Nack() {
  ScopedModule<PacketProtocol> in_pp(protocol_.get());
  if (final_receive_state_ != ReceiveState::UNKNOWN) {
    OVERNET_TRACE(DEBUG) << "Forcefully nack received packet: seq=" << seq_idx_;
    final_receive_state_ = ReceiveState::NOT_RECEIVED;
  }
}

PacketProtocol::ProcessedPacket::~ProcessedPacket() {
  ScopedModule<PacketProtocol> in_pp(protocol_.get());
  if (final_receive_state_ != ReceiveState::UNKNOWN) {
    auto it = protocol_->received_packets_.find(seq_idx_);
    assert(it != protocol_->received_packets_.end());
    assert(it->second.state == ReceiveState::UNKNOWN);
    it->second.state = final_receive_state_;
    if (final_receive_state_ == ReceiveState::NOT_RECEIVED) {
      // Clear ack pacing state to force transmission of nack ASAP.
      protocol_->last_ack_send_ = TimeStamp::Epoch();
      send_ack_ = SendAck::FORCE;
    }
  }
  switch (send_ack_) {
    case SendAck::NONE:
      break;
    case SendAck::FORCE:
      protocol_->MaybeForceAck();
      break;
    case SendAck::SCHEDULE:
      protocol_->MaybeScheduleAck();
      break;
  }
  if (ack_actions_.has_value()) {
    protocol_->RunAckActions(ack_actions_.get(), Status::Unavailable());
  }
  OVERNET_TRACE(DEBUG) << "Finish processing packet";
}

void PacketProtocol::RunAckActions(AckActions* ack_actions,
                                   const Status& nack_status) {
  if (ack_actions->bbr_ack.has_value()) {
    outgoing_bbr_.OnAck(*ack_actions->bbr_ack);
  }

  for (auto& rq : ack_actions->nacks) {
    OVERNET_TRACE(DEBUG) << "Nack message";
    rq.Ack(nack_status);
  }
  for (auto& rq : ack_actions->acks) {
    OVERNET_TRACE(DEBUG) << "Ack message";
    rq.Ack(Status::Ok());
  }

  OVERNET_TRACE(DEBUG) << "ContinueSending?";

  // Continue sending if we can
  ContinueSending();
}

bool PacketProtocol::AckIsNeeded() const { return max_seen_ > recv_tip_; }

std::string PacketProtocol::AckDebugText() {
  std::ostringstream out;
  bool first = true;
  for (const auto& r : received_packets_) {
    if (!first) {
      out << ", ";
    }
    first = false;
    out << r.first << ":";
    switch (r.second.state) {
      case ReceiveState::UNKNOWN:
        out << "UNKNOWN";
        break;
      case ReceiveState::NOT_RECEIVED:
        out << "NOT_RECEIVED";
        break;
      case ReceiveState::RECEIVED:
        out << "RECEIVED";
        break;
      case ReceiveState::RECEIVED_AND_SUPPRESSED_ACK:
        out << "RECEIVED_AND_SUPPRESSED_ACK";
        break;
    }
    out << r.second.when;
  }
  return out.str();
}

Optional<AckFrame> PacketProtocol::GenerateAck(uint32_t max_length) {
  TimeStamp now = timer_->Now();
  OVERNET_TRACE(DEBUG) << "GenerateAck: " << AckDebugText();
  auto frame = GenerateAckTo(now, max_seen_);
  if (frame.has_value()) {
    const bool adjusted_ack =
        frame->AdjustForMSS(max_length, [this, now](uint64_t seq_idx) {
          return DelayForReceivedPacket(now, seq_idx).as_us();
        });
    if (adjusted_ack) {
      MaybeScheduleAck();
    }
  }
  OVERNET_TRACE(DEBUG) << "GenerateAck generates:" << frame;
  return frame;
}

TimeDelta PacketProtocol::DelayForReceivedPacket(TimeStamp now,
                                                 uint64_t seq_idx) {
  auto it = received_packets_.lower_bound(seq_idx);
  if (it == received_packets_.end()) {
    return TimeDelta::PositiveInf();
  }
  return now - it->second.when;
}

Optional<AckFrame> PacketProtocol::GenerateAckTo(TimeStamp now,
                                                 uint64_t max_seen) {
  OVERNET_TRACE(DEBUG) << "GenerateAckTo: max_seen=" << max_seen
                       << " recv_tip=" << recv_tip_
                       << " n=" << (max_seen_ - recv_tip_);
  if (max_seen <= recv_tip_) {
    return Nothing;
  }
  if (last_ack_send_ + QuarterRTT() > now) {
    MaybeScheduleAck();
    return Nothing;
  }
  AckFrame ack(max_seen, DelayForReceivedPacket(now, max_seen).as_us());
  if (max_seen >= 1) {
    for (uint64_t seq = max_seen; seq > recv_tip_; seq--) {
      auto it = received_packets_.lower_bound(seq);
      if (it == received_packets_.end()) {
        OVERNET_TRACE(DEBUG)
            << "Mark unseen packet " << seq << " as NOT_RECEIVED";
        received_packets_.insert(
            it, std::make_pair(
                    seq, ReceivedPacket{ReceiveState::NOT_RECEIVED, now}));
        ack.AddNack(seq);
      } else if (it->first != seq) {
        OVERNET_TRACE(DEBUG)
            << "Mark unseen packet " << seq
            << " as NOT_RECEIVED (looking at seq " << it->first << ")";
        received_packets_.insert(
            it, std::make_pair(
                    seq, ReceivedPacket{ReceiveState::NOT_RECEIVED, now}));
        ack.AddNack(seq);
      } else {
        switch (it->second.state) {
          case ReceiveState::UNKNOWN: {
            OVERNET_TRACE(DEBUG)
                << "Excluding processing packet from generated ack";
            auto out = GenerateAckTo(now, seq - 1);
            MaybeScheduleAck();
            return out;
          }
          case ReceiveState::NOT_RECEIVED:
            ack.AddNack(seq);
            break;
          case ReceiveState::RECEIVED:
          case ReceiveState::RECEIVED_AND_SUPPRESSED_ACK:
            break;
        }
      }
    }
  }
  last_ack_send_ = now;
  return std::move(ack);
}

void PacketProtocol::MaybeForceAck() {
  if (ack_scheduler_.has_value()) {
    ack_scheduler_->Cancel();
    ack_scheduler_.Reset();
  }
  MaybeSendAck();
}

TimeDelta PacketProtocol::QuarterRTT() const {
  auto est = outgoing_bbr_.rtt();
  if (est == TimeDelta::PositiveInf()) {
    est = TimeDelta::FromMilliseconds(100);
  }
  return est / 4;
}

void PacketProtocol::MaybeScheduleAck() {
  if (!ack_scheduler_.has_value()) {
    OVERNET_TRACE(DEBUG) << "Schedule ack";
    ack_scheduler_.Reset(
        timer_, timer_->Now() + QuarterRTT(),
        [self = OutstandingOp<kMaybeScheduleAck>(this)](const Status& status) {
          if (status.is_error())
            return;
          self->ack_scheduler_.Reset();
          self->MaybeSendAck();
        });
  } else {
    OVERNET_TRACE(DEBUG) << "Ack already scheduled";
  }
}

void PacketProtocol::MaybeSendAck() {
  OVERNET_TRACE(DEBUG) << "MaybeSendAck: max_seen=" << max_seen_
                       << " recv_tip=" << recv_tip_
                       << " n=" << (max_seen_ - recv_tip_)
                       << " last_ack_send=" << last_ack_send_
                       << " 1/4-rtt=" << QuarterRTT()
                       << " sending=" << (sending_ ? "true" : "false");
  if (AckIsNeeded()) {
    if (sending_) {
      ack_after_sending_ = true;
    } else if (last_ack_send_ + QuarterRTT() > timer_->Now()) {
      MaybeScheduleAck();
    } else if (!ack_only_message_outstanding_) {
      ack_only_message_outstanding_ = true;
      Send(SendRequestHdl(&ack_only_send_request_));
    }
  }
}

void PacketProtocol::KeepAlive() {
  last_keepalive_event_ = timer_->Now();
  OVERNET_TRACE(DEBUG) << "KeepAlive " << last_keepalive_event_
                       << " rto=" << RetransmissionDeadline();
  if (!rto_scheduler_ && state_ == State::READY) {
    ScheduleRTO();
  }
}

void PacketProtocol::ScheduleRTO() {
  assert(state_ == State::READY);
  rto_scheduler_.Reset(
      timer_, RetransmissionDeadline(),
      StatusCallback(
          ALLOCATED_CALLBACK,  // TODO(ctiller): remove allocated
          [self = OutstandingOp<kScheduleRTO>(this)](const Status& status) {
            ScopedModule<PacketProtocol> in_pp(self.get());
            auto now = self->timer_->Now();
            OVERNET_TRACE(DEBUG)
                << "RTO check: now=" << now << " status=" << status
                << " rto=" << self->RetransmissionDeadline();
            if (status.is_error()) {
              if (now.after_epoch() == TimeDelta::PositiveInf()) {
                // Shutting down - help by nacking everything.
                self->NackBefore(now, Status::Cancelled());
              }
              return;
            }
            if (now >= self->RetransmissionDeadline()) {
              self->rto_scheduler_.Reset();
              self->NackBefore(self->last_keepalive_event_,
                               Status::Unavailable());
              if (self->LastRTOableSequence(
                      TimeStamp::AfterEpoch(TimeDelta::PositiveInf()))) {
                self->KeepAlive();
              }
            } else {
              self->ScheduleRTO();
            }
          }));
}

Optional<uint64_t> PacketProtocol::LastRTOableSequence(TimeStamp epoch) {
  OVERNET_TRACE(DEBUG) << "Check last RTO before " << epoch;
  auto last_sent = send_tip_ + outstanding_.size() - 1;
  // Some packets may be outstanding but not sent: there's no reason to nack
  // them!
  while (last_sent >= send_tip_) {
    const auto& outstanding_packet = outstanding_[last_sent - send_tip_];
    OVERNET_TRACE(DEBUG) << "Check seq:" << last_sent
                         << " (idx:" << (last_sent - send_tip_) << ") bbr_send="
                         << outstanding_packet.bbr_sent_packet.Map(
                                [](const auto& bbr_pkt) {
                                  return bbr_pkt.send_time;
                                })
                         << " pure_ack=" << outstanding_packet.is_pure_ack
                         << " has_req=" << !outstanding_packet.request.empty();
    if (outstanding_packet.scheduled <= epoch &&
        !outstanding_packet.is_pure_ack) {
      break;
    }
    last_sent--;
  }
  if (last_sent < send_tip_) {
    return Nothing;
  }
  return last_sent;
}

void PacketProtocol::NackBefore(TimeStamp epoch, const Status& nack_status) {
  auto last_sent = LastRTOableSequence(epoch);
  if (!last_sent.has_value()) {
    return;
  }
  AckFrame f(*last_sent, 0);
  for (uint64_t i = *last_sent; i >= send_tip_; i--) {
    f.AddNack(i);
  }
  auto r = HandleAck(f, true);
  assert(r.is_ok());
  RunAckActions(r.get(), nack_status);
}

TimeStamp PacketProtocol::RetransmissionDeadline() const {
  auto rtt =
      std::max(TimeDelta::FromMilliseconds(1),
               std::min(outgoing_bbr_.rtt(), TimeDelta::FromMilliseconds(250)));
  OVERNET_TRACE(DEBUG) << "RTTDL: last_keepalive=" << last_keepalive_event_
                       << " rtt=" << rtt << " => "
                       << (last_keepalive_event_ + 4 * rtt);
  return last_keepalive_event_ + 4 * rtt;
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
