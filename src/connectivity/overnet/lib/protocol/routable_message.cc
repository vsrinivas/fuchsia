// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/protocol/routable_message.h"

namespace overnet {

static const uint64_t kMaxDestCount = 128;
static const uint64_t kDestCountFlagsShift = 4;

size_t RoutableMessage::HeaderLength(NodeId writer, NodeId target,
                                     HeaderInfo* hinf) const {
  // Measure the size of the serialized message, consisting of:
  //   flags: varint64
  //   source: NodeId if !local
  //   destinations: array of Destination
  // where Destination is:
  //   node: NodeId if !local
  //   stream: StreamId
  //   sequence: SeqNum if !control
  size_t header_length = 0;
  const bool is_local =
      (src_ == writer && dsts_.size() == 1 && dsts_[0].dst() == target);
  const uint64_t dst_count = is_local ? 0 : dsts_.size();
  const uint64_t flags = (dst_count << kDestCountFlagsShift);
  const auto flags_length = varint::WireSizeFor(flags);
  if (hinf != nullptr) {
    hinf->is_local = is_local;
    hinf->flags = flags;
    hinf->flags_length = flags_length;
  }
  header_length += flags_length;
  if (!is_local) {
    header_length += src_.wire_length();
  }
  for (const auto& dst : dsts_) {
    if (!is_local)
      header_length += dst.dst_.wire_length();
    auto dst_stream_id_len = dst.stream_id().wire_length();
    if (hinf != nullptr) {
      hinf->stream_id_len.push_back(dst_stream_id_len);
    }
    header_length += dst_stream_id_len;
    header_length += dst.seq().wire_length();
  }
  return header_length;
}

Optional<size_t> RoutableMessage::MaxPayloadLength(
    NodeId writer, NodeId target, size_t remaining_space) const {
  auto hlen = HeaderLength(writer, target, nullptr);
  if (hlen + 1 > remaining_space) {
    return Nothing;
  }
  return varint::MaximumLengthWithPrefix(remaining_space - hlen);
}

Slice RoutableMessage::Write(NodeId writer, NodeId target,
                             Slice payload) const {
  HeaderInfo hinf;
  // Serialize the message.
  return payload.WithPrefix(
      HeaderLength(writer, target, &hinf), [this, &hinf](uint8_t* data) {
        uint8_t* p = data;
        p = varint::Write(hinf.flags, hinf.flags_length, p);
        if (!hinf.is_local)
          p = src_.Write(p);
        for (size_t i = 0; i < dsts_.size(); i++) {
          if (!hinf.is_local)
            p = dsts_[i].dst().Write(p);
          p = dsts_[i].stream_id().Write(hinf.stream_id_len[i], p);
          p = dsts_[i].seq().Write(p);
        }
      });
}

StatusOr<MessageWithPayload> RoutableMessage::Parse(Slice data, NodeId reader,
                                                    NodeId writer) {
  uint64_t flags;
  const uint8_t* bytes = data.begin();
  const uint8_t* end = data.end();
  if (!varint::Read(&bytes, end, &flags)) {
    return StatusOr<MessageWithPayload>(StatusCode::INVALID_ARGUMENT,
                                        "Failed to parse routing flags");
  }
  uint64_t dst_count = flags >> kDestCountFlagsShift;
  const bool is_local = dst_count == 0;
  if (is_local)
    dst_count++;
  if (dst_count > kMaxDestCount) {
    return StatusOr<MessageWithPayload>(
        StatusCode::INVALID_ARGUMENT,
        "Destination count too high in routing header");
  }
  uint64_t src;
  if (!is_local) {
    if (!ParseLE64(&bytes, end, &src)) {
      return StatusOr<MessageWithPayload>(StatusCode::INVALID_ARGUMENT,
                                          "Failed to parse source node");
    }
  } else {
    src = writer.get();
  }
  std::vector<Destination> destinations;
  destinations.reserve(dst_count);
  for (uint64_t i = 0; i < dst_count; i++) {
    uint64_t dst;
    if (!is_local) {
      if (!ParseLE64(&bytes, end, &dst)) {
        return StatusOr<MessageWithPayload>(StatusCode::INVALID_ARGUMENT,
                                            "Failed to parse destination node");
      }
    } else {
      dst = reader.get();
    }
    uint64_t stream_id;
    if (!varint::Read(&bytes, end, &stream_id)) {
      return StatusOr<MessageWithPayload>(
          StatusCode::INVALID_ARGUMENT,
          "Failed to parse stream id from routing header");
    }
    auto seq_num = SeqNum::Parse(&bytes, end);
    if (seq_num.is_error()) {
      return StatusOr<MessageWithPayload>(seq_num.AsStatus());
    }
    destinations.emplace_back(NodeId(dst), StreamId(stream_id), *seq_num.get());
  }
  return MessageWithPayload{
      RoutableMessage(NodeId(src), std::move(destinations)),
      data.FromPointer(bytes)};
}

std::ostream& operator<<(std::ostream& out, const RoutableMessage& h) {
  out << "RoutableMessage{src:" << h.src() << " dsts:{";
  int i = 0;
  for (const auto& dst : h.destinations()) {
    if (i)
      out << " ";
    out << "[" << (i++) << "] dst:" << dst.dst()
        << " stream_id:" << dst.stream_id() << " seq:" << dst.seq();
  }
  return out << "}}";
}

}  // namespace overnet
