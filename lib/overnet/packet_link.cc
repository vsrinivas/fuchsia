// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "packet_link.h"
#include <iostream>

namespace overnet {

static uint64_t GenerateLabel() {
  static uint64_t next_label = 1;
  return next_label++;
}

PacketLink::PacketLink(Router* router, NodeId peer, uint32_t mss)
    : router_(router),
      peer_(peer),
      label_(GenerateLabel()),
      protocol_{router_->timer(), this, mss} {}

void PacketLink::Forward(Message message) {
  bool send_immediately = !sending_ && outgoing_.empty();
  outgoing_.emplace(std::move(message));
  if (send_immediately) BuildAndSendPacket();
}

LinkMetrics PacketLink::GetLinkMetrics() {
  LinkMetrics m(router_->node_id(), peer_, metrics_version_++, label_);
  m.set_bw_link(protocol_.BottleneckBandwidth());
  m.set_rtt(protocol_.RoundTripTime());
  return m;
}

void PacketLink::BuildAndSendPacket() {
  assert(!sending_ && !outgoing_.empty());
  sending_ = true;
  protocol_.Send([this](uint64_t desired_prefix, uint64_t max_length) {
    while (!outgoing_.empty()) {
      Message& msg = outgoing_.front();
      if (msg.wire.payload().length() > max_length) {
        break;
      }
      auto serialized = msg.wire.Write(router_->node_id(), peer_);
      const auto serialized_length = serialized.length();
      const auto length_length = varint::WireSizeFor(serialized_length);
      const auto segment_length = length_length + serialized_length;
      if (segment_length > max_length) {
        break;
      }
      send_slices_.push_back(serialized.WithPrefix(
          length_length, [length_length, serialized_length](uint8_t* p) {
            varint::Write(serialized_length, length_length, p);
          }));
      max_length -= segment_length;
      sending_callbacks_.emplace_back(std::move(msg.done));
      outgoing_.pop();
    }

    Slice send = Slice::Join(send_slices_.begin(), send_slices_.end(),
                             desired_prefix + SeqNum::kMaxWireLength);
    send_slices_.clear();

    return PacketProtocol::SendData{
        send,
        [this](const Status& status) {
          for (auto& cb : sending_callbacks_) {
            cb(status);
          }
          sending_callbacks_.clear();
          sending_ = false;
          if (status.is_ok() && !sending_ && !outgoing_.empty()) {
            BuildAndSendPacket();
          }
        },
        PacketProtocol::SendCallback::Ignored()};
  });
}

void PacketLink::SendPacket(SeqNum seq, Slice data, StatusCallback done) {
  Emit(data.WithPrefix(1 + seq.wire_length(), [seq](uint8_t* p) {
    *p++ = 0;
    seq.Write(p);
  }));
  done(Status::Ok());
}

void PacketLink::Process(TimeStamp received, Slice packet) {
  const uint8_t* const begin = packet.begin();
  const uint8_t* p = begin;
  const uint8_t* const end = packet.end();

  if (p == end) {
    std::cerr << "Short packet received (no op code)\n";
    return;
  }
  if (*p != 0) {
    std::cerr << "Non-zero op-code received in PacketLink\n";
    return;
  }
  ++p;

  // Packets without sequence numbers are used to end the three way handshake.
  if (p == end) return;

  auto seq_status = SeqNum::Parse(&p, end);
  if (seq_status.is_error()) {
    std::cerr << "Packet seqnum parse failure: " << seq_status.AsStatus()
              << "\n";
    return;
  }
  packet.TrimBegin(p - begin);
  // begin, p, end are no longer valid.
  auto packet_status =
      protocol_.Process(received, *seq_status.get(), std::move(packet));
  if (packet_status.is_error()) {
    std::cerr << "Packet header parse failure: " << packet_status.AsStatus()
              << "\n";
    return;
  }
  if (*packet_status.get()) {
    auto body_status =
        ProcessBody(received, std::move(*packet_status.get()->get()));
    if (body_status.is_error()) {
      std::cerr << "Packet body parse failure: " << body_status << std::endl;
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
    if (msg_status.is_error()) return msg_status.AsStatus();
    router_->Forward(Message{std::move(*msg_status.get()), received,
                             StatusCallback::Ignored()});
  }
  return Status::Ok();
}

}  // namespace overnet
