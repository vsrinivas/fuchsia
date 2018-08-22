// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "packet_link.h"
#include <iostream>
#include <sstream>

namespace overnet {

static uint64_t GenerateLabel() {
  static uint64_t next_label = 1;
  return next_label++;
}

PacketLink::PacketLink(Router* router, TraceSink trace_sink, NodeId peer,
                       uint32_t mss)
    : router_(router),
      timer_(router->timer()),
      trace_sink_(trace_sink.Decorate([this](const std::string& msg) {
        std::ostringstream out;
        out << "PktLnk[" << this << "] " << msg;
        return out.str();
      })),
      peer_(peer),
      label_(GenerateLabel()),
      protocol_{router_->timer(), this, trace_sink_, mss} {}

void PacketLink::Close(Callback<void> quiesced) {
  stashed_.Reset();
  while (!outgoing_.empty()) {
    outgoing_.pop();
  }
  if (emitting_) {
    emitting_->timeout.Cancel();
  }
  protocol_.Close(std::move(quiesced));
}

void PacketLink::Forward(Message message) {
  bool send_immediately = !sending_ && outgoing_.empty();
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "Forward sending=" << sending_ << " outgoing=" << outgoing_.size()
      << " imm=" << send_immediately;
  outgoing_.emplace(std::move(message));
  if (send_immediately) {
    SchedulePacket();
  }
}

LinkMetrics PacketLink::GetLinkMetrics() {
  LinkMetrics m(router_->node_id(), peer_, metrics_version_++, label_);
  m.set_bw_link(protocol_.BottleneckBandwidth());
  m.set_rtt(protocol_.RoundTripTime());
  m.set_mss(std::max(8u, protocol_.mss()) - 8);
  return m;
}

void PacketLink::SchedulePacket() {
  assert(!sending_);
  assert(!outgoing_.empty() || stashed_.has_value());
  sending_ = true;
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "SchedulePacket outgoing=" << outgoing_.size();
  protocol_.Send(
      [this](auto arg) {
        auto pkt = BuildPacket(arg);
        sending_ = false;
        if (stashed_.has_value() || !outgoing_.empty()) {
          SchedulePacket();
        }
        return pkt;
      },
      PacketProtocol::SendCallback::Ignored());
}

Slice PacketLink::BuildPacket(LazySliceArgs args) {
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "BuildPacket outgoing=" << outgoing_.size() << " stashed="
      << (stashed_ ? [&](){ 
        std::ostringstream fmt;
        fmt << stashed_->message << "+" << stashed_->payload.length() << "b";
        return fmt.str();
      }() : "nil");
  auto remaining_length = args.max_length;
  auto add_serialized_msg = [&remaining_length, this](
                                const RoutableMessage& wire,
                                Slice payload) -> bool {
    auto serialized = wire.Write(router_->node_id(), peer_, std::move(payload));
    const auto serialized_length = serialized.length();
    const auto length_length = varint::WireSizeFor(serialized_length);
    const auto segment_length = length_length + serialized_length;
    OVERNET_TRACE(DEBUG, trace_sink_)
        << "BuildPacket/AddMsg segment_length=" << segment_length
        << " remaining_length=" << remaining_length;
    if (segment_length > remaining_length) {
      return false;
    }
    send_slices_.push_back(serialized.WithPrefix(
        length_length, [length_length, serialized_length](uint8_t* p) {
          varint::Write(serialized_length, length_length, p);
        }));
    remaining_length -= segment_length;
    return true;
  };

  static const uint32_t kMinMSS = 64;
  if (stashed_.has_value()) {
    if (add_serialized_msg(stashed_->message, stashed_->payload)) {
      stashed_.Reset();
    } else {
      if (args.has_other_content) {
        // Skip sending any other messages: we'll retry this message
        // without an ack momentarily.
        remaining_length = 0;
      } else {
        // There's no chance we'll ever send this message: drop it.
        stashed_.Reset();
        OVERNET_TRACE(DEBUG, trace_sink_) << " drop stashed";
      }
    }
  }
  while (!outgoing_.empty() && remaining_length > kMinMSS) {
    // Ensure there's space with the routing header included.
    Optional<size_t> max_len_before_prefix =
        outgoing_.front().header.MaxPayloadLength(router_->node_id(), peer_,
                                                  remaining_length);
    if (!max_len_before_prefix.has_value() || *max_len_before_prefix <= 1) {
      break;
    }
    // And ensure there's space with the segment length header.
    auto max_len = varint::MaximumLengthWithPrefix(*max_len_before_prefix);
    // Pull out the message.
    Message msg = std::move(outgoing_.front());
    outgoing_.pop();
    // Serialize it.
    auto payload = msg.make_payload(
        LazySliceArgs{0, static_cast<uint32_t>(max_len),
                      args.has_other_content || !send_slices_.empty(),
                      args.delay_until_time});
    OVERNET_TRACE(DEBUG, trace_sink_)
        << "delay -> " << (*args.delay_until_time - timer_->Now());
    // Add the serialized version to the outgoing queue.
    if (!add_serialized_msg(msg.header, payload)) {
      // If it fails, stash it, and retry the next loop around.
      // This may happen if the sender is unable to trim to the maximum length.
      OVERNET_TRACE(DEBUG, trace_sink_) << " stash too long";
      stashed_.Reset(std::move(msg.header), std::move(payload));
      break;
    }
  }

  Slice send = Slice::Join(send_slices_.begin(), send_slices_.end(),
                           args.desired_prefix + SeqNum::kMaxWireLength);
  send_slices_.clear();

  return send;
}

void PacketLink::SendPacket(SeqNum seq, LazySlice data, Callback<void> done) {
  assert(!emitting_);
  const auto prefix_length = 1 + seq.wire_length();
  const TimeStamp now = timer_->Now();
  TimeStamp send_time = now;
  auto data_slice = data(LazySliceArgs{
      prefix_length, protocol_.mss() - prefix_length, false, &send_time});
  auto send_slice = data_slice.WithPrefix(prefix_length, [seq](uint8_t* p) {
    *p++ = 0;
    seq.Write(p);
  });
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "StartEmit " << send_slice << " delay=" << (send_time - now);
  emitting_.Reset(timer_, send_time, std::move(send_slice), std::move(done),
                  [this](const Status& status) {
                    OVERNET_TRACE(DEBUG, trace_sink_)
                        << "Emit status=" << status;
                    if (status.is_ok()) {
                      Emit(std::move(emitting_->slice));
                    }
                    auto cb = std::move(emitting_->done);
                    emitting_.Reset();
                  });
}

void PacketLink::Process(TimeStamp received, Slice packet) {
  const uint8_t* const begin = packet.begin();
  const uint8_t* p = begin;
  const uint8_t* const end = packet.end();

  if (p == end) {
    OVERNET_TRACE(WARNING, trace_sink_) << "Short packet received (no op code)";
    return;
  }
  if (*p != 0) {
    OVERNET_TRACE(WARNING, trace_sink_)
        << "Non-zero op-code received in PacketLink";
    return;
  }
  ++p;

  // Packets without sequence numbers are used to end the three way handshake.
  if (p == end)
    return;

  auto seq_status = SeqNum::Parse(&p, end);
  if (seq_status.is_error()) {
    OVERNET_TRACE(WARNING, trace_sink_)
        << "Packet seqnum parse failure: " << seq_status.AsStatus();
    return;
  }
  packet.TrimBegin(p - begin);
  // begin, p, end are no longer valid.
  auto packet_status =
      protocol_.Process(received, *seq_status.get(), std::move(packet));
  if (packet_status.status.is_error()) {
    OVERNET_TRACE(WARNING, trace_sink_)
        << "Packet header parse failure: " << packet_status.status.AsStatus();
    return;
  }
  if (*packet_status.status.get()) {
    auto body_status =
        ProcessBody(received, std::move(*packet_status.status.get()->get()));
    if (body_status.is_error()) {
      OVERNET_TRACE(WARNING, trace_sink_)
          << "Packet body parse failure: " << body_status << std::endl;
      return;
    }
  }
}

Status PacketLink::ProcessBody(TimeStamp received, Slice packet) {
  while (packet.length()) {
    const uint8_t* const begin = packet.begin();
    const uint8_t* p = begin;
    const uint8_t* const end = packet.end();

    uint64_t serialized_length;
    if (!varint::Read(&p, end, &serialized_length)) {
      return Status(StatusCode::INVALID_ARGUMENT,
                    "Failed to parse segment length");
    }
    assert(end >= p);
    if (static_cast<uint64_t>(end - p) < serialized_length) {
      return Status(StatusCode::INVALID_ARGUMENT,
                    "Message body extends past end of packet");
    }
    packet.TrimBegin(p - begin);
    auto msg_status = RoutableMessage::Parse(
        packet.TakeUntilOffset(serialized_length), router_->node_id(), peer_);
    if (msg_status.is_error())
      return msg_status.AsStatus();
    router_->Forward(Message::SimpleForwarder(std::move(msg_status->message),
                                              std::move(msg_status->payload),
                                              received));
  }
  return Status::Ok();
}

}  // namespace overnet
