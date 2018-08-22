// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "packet_protocol.h"
#include <iostream>

namespace overnet {

static const char kClosing[] = "Closing";
static const char kMaybeScheduleAck[] = "MaybeScheduleAck";
static const char kMaybeSendAck[] = "MaybeSendAck";
static const char kRequestSendAck[] = "RequestSendAck";
static const char kRequestTransmit[] = "RequestTransmit";
static const char kScheduleRTO[] = "ScheduleRTO";
static const char kStartNext[] = "StartNext";
static const char kTransmitPacket[] = "TransmitPacket";

const char PacketProtocol::kProcessedPacket[] = "ProcessedPacket";

void PacketProtocol::Close(Callback<void> quiesced) {
  assert(state_ == State::READY);
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "Close outstanding_ops=" << outstanding_ops_;
  OutstandingOp<kClosing> op(this);
  state_ = State::CLOSING;
  quiesced_ = std::move(quiesced);
  // Stop waiting for things.
  rto_scheduler_.Reset();
  ack_scheduler_.Reset();
  outgoing_bbr_.CancelRequestTransmit();
  NackAll();
  decltype(queued_) queued;
  queued.swap(queued_);
  queued.clear();
}

void PacketProtocol::RequestSendAck() {
  // Prevent quiescing during ack generation (otherwise cancelling a scheduled
  // ack might cause us to quiesce).
  OutstandingOp<kRequestSendAck> op(this);
  MaybeForceAck();
}

void PacketProtocol::Send(LazySlice make_payload, SendCallback on_ack) {
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "Send state=" << static_cast<int>(state_)
      << " qsize=" << queued_.size() << " outstanding=" << outstanding_.size()
      << " sending=" << sending_.has_value();
  if (state_ != State::READY) {
    // Discard result, forcing callbacks to be made
    return;
  }
  MaybeSendSlice(QueuedPacket{std::move(make_payload), std::move(on_ack)});
}

void PacketProtocol::MaybeSendSlice(QueuedPacket&& packet) {
  if (!queued_.empty() || sending_) {
    queued_.emplace_back(std::forward<QueuedPacket>(packet));
    return;
  }
  SendSlice(std::forward<QueuedPacket>(packet));
}

void PacketProtocol::SendSlice(QueuedPacket&& packet) {
  sending_.Reset(std::forward<QueuedPacket>(packet));
  OVERNET_TRACE(DEBUG, trace_sink_) << "SendSlice send_tip=" << send_tip_
                                    << " outstanding=" << outstanding_.size();
  outgoing_bbr_.RequestTransmit([self = OutstandingOp<kRequestTransmit>(this)](
                                    const Status& status) mutable {
    if (status.is_error()) {
      self->sending_->on_ack(status);
      self->sending_.Reset();
      return;
    }
    self->TransmitPacket();
  });
}

void PacketProtocol::TransmitPacket() {
  const uint64_t seq_idx = send_tip_ + outstanding_.size();
  if (seq_idx - send_tip_ > max_outstanding_size_) {
    max_outstanding_size_ = seq_idx - send_tip_;
  }
  SeqNum seq_num(seq_idx, max_outstanding_size_);
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "TransmitPacket seq=" << seq_idx << " -> " << seq_num
      << " (send_tip=" << send_tip_ << ")";
  if (outstanding_.empty()) {
    KeepAlive();
  }
  outstanding_.emplace_back(
      OutstandingPacket{max_seen_, Nothing, std::move(sending_->on_ack)});
  auto send_fn = std::move(sending_->payload_factory);
  send_fn.AddMutator([seq_idx, self = OutstandingOp<kTransmitPacket>(this)](
                         auto payload, LazySliceArgs args) {
    OVERNET_TRACE(DEBUG, self->trace_sink_) << "GeneratePacket seq=" << seq_idx;
    const auto outstanding_idx = seq_idx - self->send_tip_;
    if (outstanding_idx >= self->outstanding_.size())
      return Slice();
    if (self->outstanding_[outstanding_idx].on_ack.empty())
      return Slice();
    auto slice = self->GeneratePacket(std::move(payload), args);
    assert(!self->outstanding_[outstanding_idx].bbr_sent_packet.has_value());
    self->outstanding_[outstanding_idx].bbr_sent_packet =
        self->outgoing_bbr_.ScheduleTransmit(
            args.delay_until_time,
            BBR::OutgoingPacket{seq_idx, slice.length()});
    return slice;
  });
  packet_sender_->SendPacket(seq_num, std::move(send_fn),
                             [self = OutstandingOp<kStartNext>(this)]() {
                               self->sending_.Reset();
                               self->ContinueSending();
                             });
}

Slice PacketProtocol::GeneratePacket(LazySlice payload, LazySliceArgs args) {
  auto ack = GenerateAck();
  if (ack) {
    AckFrame::Writer ack_writer(ack.get());
    const uint8_t ack_length_length =
        varint::WireSizeFor(ack_writer.wire_length());
    const uint64_t prefix_length = ack_length_length + ack_writer.wire_length();
    auto payload_slice = payload(LazySliceArgs{
        args.desired_prefix + prefix_length, args.max_length - prefix_length,
        true, args.delay_until_time});
    return payload_slice.WithPrefix(
        prefix_length, [&ack_writer, ack_length_length](uint8_t* p) {
          ack_writer.Write(
              varint::Write(ack_writer.wire_length(), ack_length_length, p));
        });
  } else {
    auto payload_slice =
        payload(LazySliceArgs{args.desired_prefix + 1, args.max_length - 1,
                              args.has_other_content, args.delay_until_time});
    return payload_slice.WithPrefix(1, [](uint8_t* p) { *p = 0; });
  }
}

Status PacketProtocol::HandleAck(const AckFrame& ack) {
  OVERNET_TRACE(DEBUG, trace_sink_) << "HandleAck: " << ack;

  // TODO(ctiller): inline vectors to avoid allocations.
  std::vector<SendCallback> acks;
  std::vector<SendCallback> nacks;
  BBR::Ack bbr_ack;

  // Validate ack, and ignore if it's old.
  if (ack.ack_to_seq() < send_tip_)
    return Status::Ok();
  if (ack.ack_to_seq() >= send_tip_ + outstanding_.size()) {
    return Status(StatusCode::INVALID_ARGUMENT,
                  "Ack packet past sending sequence");
  }
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
  for (auto nack_seq : ack.nack_seqs()) {
    if (nack_seq < send_tip_) {
      continue;
    }
    if (nack_seq >= send_tip_ + outstanding_.size()) {
      return Status(StatusCode::INVALID_ARGUMENT, "Nack past sending sequence");
    }
    OutstandingPacket& pkt = outstanding_[nack_seq - send_tip_];
    auto cb = std::move(pkt.on_ack);
    if (!cb.empty()) {
      nacks.emplace_back(std::move(cb));
    }
    if (pkt.bbr_sent_packet.has_value()) {
      bbr_ack.nacked_packets.push_back(*pkt.bbr_sent_packet);
    }
  }
  // Clear out outstanding packet references, propagating acks.
  while (send_tip_ <= ack.ack_to_seq()) {
    OutstandingPacket& pkt = outstanding_.front();
    auto cb = std::move(pkt.on_ack);
    send_tip_++;
    if (!cb.empty()) {
      bbr_ack.acked_packets.push_back(*pkt.bbr_sent_packet);
      outstanding_.pop_front();
      acks.emplace_back(std::move(cb));
    } else {
      outstanding_.pop_front();
    }
  }
  outgoing_bbr_.OnAck(bbr_ack);

  for (auto& cb : nacks) {
    cb(Status::Cancelled());
  }
  for (auto& cb : acks) {
    cb(Status::Ok());
  }

  // Continue sending if we can
  ContinueSending();

  return Status::Ok();
}

void PacketProtocol::ContinueSending() {
  while (!queued_.empty() && !sending_ && state_ == State::READY) {
    QueuedPacket p = std::move(queued_.front());
    queued_.pop_front();
    SendSlice(std::move(p));
  }
  if (ack_after_sending_) {
    ack_after_sending_ = false;
    MaybeSendAck();
  }
}

PacketProtocol::ProcessedPacket PacketProtocol::Process(TimeStamp received,
                                                        SeqNum seq_num,
                                                        Slice slice) {
  OVERNET_TRACE(DEBUG, trace_sink_) << "Process: " << slice;

  using StatusType = StatusOr<Optional<Slice>>;
  OutstandingOp<kProcessedPacket> op(this);

  // Validate sequence number, ignore if it's old.
  const auto seq_idx = seq_num.Reconstruct(recv_tip_);
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "Receive sequence " << seq_num << "=" << seq_idx << " recv_tip "
      << recv_tip_ << " max_seen=" << max_seen_;
  if (seq_idx < recv_tip_) {
    return ProcessedPacket(op, ProcessedPacket::Ack::NONE, Nothing);
  }

  // Keep track of the biggest valid sequence we've seen.
  if (seq_idx > max_seen_) {
    OVERNET_TRACE(DEBUG, trace_sink_) << "new max_seen";
    max_seen_ = seq_idx;
    max_seen_time_ = received;
  }

  KeepAlive();

  const uint8_t* p = slice.begin();
  const uint8_t* end = slice.end();

  if (p == end) {
    return ProcessedPacket(op, ProcessedPacket::Ack::NONE, Nothing);
  }

  uint64_t ack_length;
  if (!varint::Read(&p, end, &ack_length)) {
    return ProcessedPacket(
        op, ProcessedPacket::Ack::NONE,
        StatusType(StatusCode::INVALID_ARGUMENT,
                   "Failed to parse ack length from message"));
  }
  slice.TrimBegin(p - slice.begin());

  OVERNET_TRACE(DEBUG, trace_sink_) << "ack_length=" << ack_length;

  if (ack_length > slice.length()) {
    return ProcessedPacket(
        op, ProcessedPacket::Ack::NONE,
        StatusType(StatusCode::INVALID_ARGUMENT,
                   "Ack frame claimed to be past end of message"));
  }

  ProcessedPacket::Ack ack = ProcessedPacket::Ack::NONE;

  auto it = received_packets_.lower_bound(seq_idx);
  if (it == received_packets_.end() || it->first != seq_idx) {
    it = received_packets_.insert(
        it, std::make_pair(seq_idx, ReceivedPacket{true, false}));
  } else {
    OVERNET_TRACE(DEBUG, trace_sink_)
        << "frozen as " << (it->second.received ? "received" : "nack");
    return ProcessedPacket(op, ProcessedPacket::Ack::NONE, Nothing);
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
    } else if (prev->second.suppressed_ack) {
      suppress_ack = false;
      prev_was_also_suppressed = true;
    }
  }
  if (suppress_ack && it->first != received_packets_.rbegin()->first) {
    suppress_ack = false;
  }
  it->second.suppressed_ack = suppress_ack;

  OVERNET_TRACE(DEBUG, trace_sink_)
      << "pure_ack=" << is_pure_ack << " suppress_ack=" << suppress_ack
      << " is_last=" << (it->first == received_packets_.rbegin()->first)
      << " prev_was_also_suppressed=" << prev_was_also_suppressed
      << " prev_was_discontiguous=" << prev_was_discontiguous;

  if (suppress_ack) {
    OVERNET_TRACE(DEBUG, trace_sink_) << "ack suppressed";
  } else {
    if (seq_idx >= kMaxUnackedReceives &&
        max_acked_ <= seq_idx - kMaxUnackedReceives) {
      ack = ProcessedPacket::Ack::FORCE;
    } else {
      ack = ProcessedPacket::Ack::SCHEDULE;
    }
  }

  if (ack_length == 0) {
    return ProcessedPacket(op, ack, slice);
  }

  return ProcessedPacket(
      op, ack,
      AckFrame::Parse(slice.TakeUntilOffset(ack_length))
          .Then([this](const AckFrame& frame) { return HandleAck(frame); })
          .Then([&slice]() -> StatusType { return slice; }));
}  // namespace overnet

bool PacketProtocol::AckIsNeeded() const { return max_seen_ > recv_tip_; }

Optional<AckFrame> PacketProtocol::GenerateAck() {
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "GenerateAck: max_seen=" << max_seen_ << " recv_tip=" << recv_tip_
      << " n=" << (max_seen_ - recv_tip_);
  if (!AckIsNeeded()) {
    return Nothing;
  }
  if (last_ack_send_ + QuarterRTT() > timer_->Now()) {
    MaybeScheduleAck();
    return Nothing;
  }
  const auto now = timer_->Now();
  last_ack_send_ = now;
  assert(max_seen_time_ <= now);
  AckFrame ack(max_seen_, (now - max_seen_time_).as_us());
  if (max_seen_ >= 1) {
    for (uint64_t seq = max_seen_ - 1; seq > recv_tip_; seq--) {
      auto it = received_packets_.lower_bound(seq);
      if (it == received_packets_.end() || it->first != seq) {
        received_packets_.insert(
            it, std::make_pair(seq, ReceivedPacket{false, false}));
        ack.AddNack(seq);
      } else if (!it->second.received) {
        ack.AddNack(seq);
      }
    }
  }
  OVERNET_TRACE(DEBUG, trace_sink_) << "GenerateAck generates:" << ack;
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
    ack_scheduler_.Reset(
        timer_, timer_->Now() + QuarterRTT(),
        [self = OutstandingOp<kMaybeScheduleAck>(this)](const Status& status) {
          if (status.is_error())
            return;
          self->ack_scheduler_.Reset();
          self->MaybeSendAck();
        });
  }
}

void PacketProtocol::MaybeSendAck() {
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "MaybeSendAck: max_seen=" << max_seen_ << " recv_tip=" << recv_tip_
      << " n=" << (max_seen_ - recv_tip_) << " last_ack_send=" << last_ack_send_
      << " 1/4-rtt=" << QuarterRTT() << " now=" << timer_->Now()
      << " sending=" << (sending_ ? "true" : "false");
  if (AckIsNeeded()) {
    if (sending_) {
      ack_after_sending_ = true;
    } else if (last_ack_send_ + QuarterRTT() > timer_->Now()) {
      MaybeScheduleAck();
    } else {
      Send([](auto) { return Slice(); },
           [self = OutstandingOp<kMaybeSendAck>(this)](const Status& status) {
             if (status.is_error() && self->state_ == State::READY) {
               self->MaybeScheduleAck();
             }
           });
    }
  }
}

void PacketProtocol::KeepAlive() {
  last_keepalive_event_ = timer_->Now();
  OVERNET_TRACE(DEBUG, trace_sink_) << "KeepAlive " << last_keepalive_event_
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
            auto now = self->timer_->Now();
            OVERNET_TRACE(DEBUG, self->trace_sink_)
                << " RTO check: now=" << now << " status=" << status
                << " rto=" << self->RetransmissionDeadline();
            if (status.is_error()) {
              if (now.after_epoch() == TimeDelta::PositiveInf()) {
                // Shutting down - help by nacking everything.
                self->NackAll();
              }
              return;
            }
            if (now >= self->RetransmissionDeadline()) {
              self->rto_scheduler_.Reset();
              self->NackAll();
            } else {
              self->ScheduleRTO();
            }
          }));
}

void PacketProtocol::NackAll() {
  if (outstanding_.empty()) {
    return;
  }
  auto last_sent = send_tip_ + outstanding_.size() - 1;
  AckFrame f(last_sent, 0);
  for (uint64_t i = last_sent; i >= send_tip_; i--) {
    f.AddNack(i);
  }
  HandleAck(f);
}

TimeStamp PacketProtocol::RetransmissionDeadline() const {
  auto rtt = std::min(outgoing_bbr_.rtt(), TimeDelta::FromSeconds(3));
  return last_keepalive_event_ + 4 * rtt;
}

}  // namespace overnet
