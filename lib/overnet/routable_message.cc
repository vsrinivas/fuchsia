// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routable_message.h"

namespace overnet {

static const uint64_t kMaxDestCount = 128;
static const uint64_t kDestCountFlagsShift = 4;
static const uint64_t kControlFlag = 0x01;

Slice RoutableMessage::Write(NodeId writer, NodeId target) const {
  // Measure the size of the serialized message, consisting of:
  //   flags: varint64
  //   source: NodeId if !local
  //   destinations: array of Destination
  // where Destination is:
  //   node: NodeId if !local
  //   stream: StreamId
  //   sequence: SeqNum if !control
  size_t header_length = 0;
  std::vector<uint8_t> stream_id_len;
  const bool is_local =
      (src_ == writer && dsts_.size() == 1 && dsts_[0].dst() == target);
  const uint64_t dst_count = is_local ? 0 : dsts_.size();
  const uint64_t flags =
      (dst_count << kDestCountFlagsShift) | (is_control() ? kControlFlag : 0);
  const auto flags_length = varint::WireSizeFor(flags);
  header_length += flags_length;
  if (!is_local) header_length += src_.wire_length();
  for (const auto& dst : dsts_) {
    if (!is_local) header_length += dst.dst_.wire_length();
    auto dst_stream_id_len = dst.stream_id().wire_length();
    stream_id_len.push_back(dst_stream_id_len);
    header_length += dst_stream_id_len;
    if (!is_control()) header_length += dst.seq()->wire_length();
  }

  // Serialize the message.
  return payload_.WithPrefix(header_length, [&](uint8_t* data) {
    uint8_t* p = data;
    p = varint::Write(flags, flags_length, p);
    if (!is_local) p = src_.Write(p);
    for (size_t i = 0; i < dsts_.size(); i++) {
      if (!is_local) p = dsts_[i].dst().Write(p);
      p = dsts_[i].stream_id().Write(stream_id_len[i], p);
      if (!is_control()) p = dsts_[i].seq()->Write(p);
    }
    assert(p == data + header_length);
  });
}

StatusOr<RoutableMessage> RoutableMessage::Parse(Slice data, NodeId reader,
                                                 NodeId writer) {
  uint64_t flags;
  const uint8_t* bytes = data.begin();
  const uint8_t* end = data.end();
  if (!varint::Read(&bytes, end, &flags)) {
    return StatusOr<RoutableMessage>(StatusCode::INVALID_ARGUMENT,
                                     "Failed to parse routing flags");
  }
  uint64_t dst_count = flags >> kDestCountFlagsShift;
  const bool is_control = (flags & kControlFlag) != 0;
  const bool is_local = dst_count == 0;
  if (is_local) dst_count++;
  if (dst_count > kMaxDestCount) {
    return StatusOr<RoutableMessage>(
        StatusCode::INVALID_ARGUMENT,
        "Destination count too high in routing header");
  }
  uint64_t src;
  if (!is_local) {
    if (!ParseLE64(&bytes, end, &src)) {
      return StatusOr<RoutableMessage>(StatusCode::INVALID_ARGUMENT,
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
        return StatusOr<RoutableMessage>(StatusCode::INVALID_ARGUMENT,
                                         "Failed to parse destination node");
      }
    } else {
      dst = reader.get();
    }
    uint64_t stream_id;
    if (!varint::Read(&bytes, end, &stream_id)) {
      return StatusOr<RoutableMessage>(
          StatusCode::INVALID_ARGUMENT,
          "Failed to parse stream id from routing header");
    }
    if (!is_control) {
      auto seq_num = SeqNum::Parse(&bytes, end);
      if (seq_num.is_error()) {
        return StatusOr<RoutableMessage>(seq_num.AsStatus());
      }
      destinations.emplace_back(NodeId(dst), StreamId(stream_id),
                                *seq_num.get());
    } else {
      destinations.emplace_back(NodeId(dst), StreamId(stream_id), Nothing);
    }
  }
  return RoutableMessage(NodeId(src), is_control, std::move(destinations),
                         data.FromPointer(bytes));
}

std::ostream& operator<<(std::ostream& out, const RoutableMessage& h) {
  out << "RoutableMessage{src:" << h.src() << " dsts:{";
  int i = 0;
  for (const auto& dst : h.destinations()) {
    if (i) out << " ";
    out << "[" << (i++) << "] dst:" << dst.dst()
        << " stream_id:" << dst.stream_id() << " seq:" << dst.seq();
  }
  return out << "}, payload=" << h.payload() << "}";
}

}  // namespace overnet
