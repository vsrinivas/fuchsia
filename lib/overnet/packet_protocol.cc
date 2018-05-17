// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "packet_protocol.h"
#include <iostream>

namespace overnet {

void PacketProtocol::MaybeSendSlice(QueuedPacket&& packet) {
  if (!queued_.empty() || outstanding_.size() >= kLookaheadWindow || sending_) {
    queued_.emplace_back(std::forward<QueuedPacket>(packet));
    return;
  }
  SendSlice(std::forward<QueuedPacket>(packet));
}

void PacketProtocol::SendSlice(QueuedPacket&& packet) {
  sending_.Reset(std::forward<QueuedPacket>(packet));
  uint64_t seq_idx = send_tip_ + outstanding_.size();
  outgoing_bbr_.RequestTransmit(
      BBR::OutgoingPacket{seq_idx, sending_->send_packet.length()},
      [this](const StatusOr<BBR::SentPacket>& status) mutable {
        if (status.is_error()) {
          sending_->sent(status.AsStatus());
          sending_->finished(status.AsStatus());
          sending_.Reset();
          return;
        }
        const auto& sent_packet = *status.get();
        SeqNum seq_num(sent_packet.outgoing.sequence,
                       sent_packet.outgoing.sequence - send_tip_);
        outstanding_.emplace_back(OutstandingPacket{
            max_seen_ + 1, sent_packet, std::move(sending_->finished)});
        packet_sender_->SendPacket(seq_num, std::move(sending_->send_packet),
                                   std::move(sending_->sent));
        sending_.Reset();
        ContinueSending();
      });
}

Status PacketProtocol::HandleAck(const AckFrame& ack) {
  // Validate ack, and ignore if it's old.
  if (ack.ack_to_seq() < send_tip_) return Status::Ok();
  if (ack.ack_to_seq() >= send_tip_ + outstanding_.size()) {
    return Status(StatusCode::INVALID_ARGUMENT,
                  "Ack packet past sending sequence");
  }
  // Move receive window forward.
  auto new_recv_tip = outstanding_[ack.ack_to_seq() - send_tip_].ack_to_seq;
  if (new_recv_tip != recv_tip_) {
    assert(new_recv_tip > recv_tip_);
    const auto move = new_recv_tip - recv_tip_;
    assert(move < kLookaheadWindow);
    received_ >>= move;
    frozen_ >>= move;
    recv_tip_ = new_recv_tip;
  }
  assert(bbr_ack_.acked_packets.empty());
  assert(bbr_ack_.nacked_packets.empty());
  // Fail any nacked packets.
  for (auto nack_seq : ack.nack_seqs()) {
    if (nack_seq < send_tip_) continue;
    if (nack_seq >= send_tip_ + outstanding_.size()) {
      return Status(StatusCode::INVALID_ARGUMENT, "Nack past sending sequence");
    }
    OutstandingPacket& pkt = outstanding_[nack_seq - send_tip_];
    auto cb = std::move(pkt.finished);
    if (!cb.empty()) {
      cb(Status::Cancelled());
      bbr_ack_.nacked_packets.push_back(pkt.bbr_sent_packet);
    }
  }
  // Clear out outstanding packet references, propagating acks.
  while (send_tip_ <= ack.ack_to_seq()) {
    OutstandingPacket& pkt = outstanding_.front();
    auto cb = std::move(pkt.finished);
    send_tip_++;
    if (!cb.empty()) {
      bbr_ack_.acked_packets.push_back(pkt.bbr_sent_packet);
      outstanding_.pop_front();
      cb(Status::Ok());
    } else {
      outstanding_.pop_front();
    }
  }
  outgoing_bbr_.OnAck(bbr_ack_);
  bbr_ack_.acked_packets.clear();
  bbr_ack_.nacked_packets.clear();
  // Continue sending if we can
  ContinueSending();
  return Status::Ok();
}

void PacketProtocol::ContinueSending() {
  while (!queued_.empty() && outstanding_.size() < kLookaheadWindow &&
         !sending_) {
    QueuedPacket p = std::move(queued_.front());
    queued_.pop_front();
    SendSlice(std::move(p));
  }
}

StatusOr<Optional<Slice>> PacketProtocol::Process(TimeStamp received,
                                                  SeqNum seq_num, Slice slice) {
  using StatusType = StatusOr<Optional<Slice>>;

  // Validate sequence number, ignore if it's old.
  const auto seq_idx = seq_num.Reconstruct(recv_tip_);
  if (seq_idx < recv_tip_) {
    return Nothing;
  }

  // Make sure we're in the region we can reason about.
  if (seq_idx >= kLookaheadWindow && seq_idx - kLookaheadWindow >= recv_tip_) {
    MaybeForceAck();
    return StatusType(StatusCode::FAILED_PRECONDITION,
                      "Too many skipped sequences");
  }

  // Keep track of the biggest valid sequence we've seen.
  if (seq_idx > max_seen_) {
    max_seen_ = seq_idx;
    max_seen_time_ = received;
  }

  if (frozen_.test(seq_idx - recv_tip_)) {
    return Nothing;
  }

  received_.set(seq_idx - recv_tip_);
  frozen_.set(seq_idx - recv_tip_);
  if (seq_idx >= kMaxUnackedReceives &&
      max_acked_ <= seq_idx - kMaxUnackedReceives) {
    MaybeForceAck();
  } else {
    MaybeScheduleAck();
  }

  const uint8_t* p = slice.begin();
  const uint8_t* end = slice.end();
  uint64_t ack_length;
  if (!varint::Read(&p, end, &ack_length)) {
    return StatusType(StatusCode::INVALID_ARGUMENT,
                      "Failed to parse ack length from message");
  }
  slice.TrimBegin(p - slice.begin());

  if (ack_length > slice.length()) {
    return StatusType(StatusCode::INVALID_ARGUMENT,
                      "Ack frame claimed to be past end of message");
  }

  if (ack_length == 0) {
    return slice;
  }

  return AckFrame::Parse(slice.TakeUntilOffset(ack_length))
      .Then([this](const AckFrame& frame) { return HandleAck(frame); })
      .Then([&slice]() -> StatusType { return slice; });
}

Optional<AckFrame> PacketProtocol::GenerateAck() {
  if (max_seen_ <= recv_tip_) return Nothing;
  const auto now = timer_->Now();
  assert(max_seen_time_ <= now);
  AckFrame ack(max_seen_, (now - max_seen_time_).as_us());
  if (max_seen_ >= 1) {
    for (uint64_t seq = max_seen_ - 1; seq >= recv_tip_; seq--) {
      if (!received_.test(seq - recv_tip_)) {
        ack.AddNack(seq);
      }
      frozen_.set(seq - recv_tip_);
    }
  }
  return std::move(ack);
}

void PacketProtocol::MaybeForceAck() {
  if (ack_scheduler_.has_value()) {
    ack_scheduler_->Cancel();
    ack_scheduler_.Reset();
  }
  MaybeSendAck();
}

void PacketProtocol::MaybeScheduleAck() {
  if (!ack_scheduler_.has_value()) {
    // TODO(ctiller): this should be k*RTT, k~=.25
    const TimeDelta ack_delay = TimeDelta::FromMilliseconds(25);
    ack_scheduler_.Reset(timer_, timer_->Now() + ack_delay,
                         [this](const Status& status) {
                           ack_scheduler_.Reset();
                           if (status.is_error()) return;
                           MaybeSendAck();
                         });
  }
}

void PacketProtocol::MaybeSendAck() {
  Send([this](uint64_t, uint64_t) {
    return SendData{Slice(), StatusCallback::Ignored(),
                    [this](const Status& status) {
                      if (status.is_error()) MaybeScheduleAck();
                    }};
  });
}

}  // namespace overnet
