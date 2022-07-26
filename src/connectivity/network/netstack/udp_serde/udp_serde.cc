// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "udp_serde.h"

#include <fidl/fuchsia.net/cpp/wire.h>
#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <netinet/in.h>

#include <span>

namespace fnet = fuchsia_net;

namespace {

constexpr uint8_t kVersioningSegmentSize = 8;
constexpr uint8_t kVersioningSegment[kVersioningSegmentSize] = {0};
constexpr cpp20::span<const uint8_t> kVersioningSegmentSpan(kVersioningSegment,
                                                            kVersioningSegmentSize);

constexpr uint8_t kMetadataSizeSegmentSize = 8;
constexpr uint8_t kMetadataSizeSize = sizeof(uint16_t);
constexpr uint8_t kMetadataSizeSegmentPaddingSize = kMetadataSizeSegmentSize - kMetadataSizeSize;
constexpr uint8_t kMetadataSizeSegmentPadding[kMetadataSizeSegmentPaddingSize] = {0};
constexpr cpp20::span<const uint8_t> kMetaSizeSegmentPaddingSpan(kMetadataSizeSegmentPadding,
                                                                 kMetadataSizeSegmentPaddingSize);

constexpr uint8_t kSumOfSegmentSizes = kVersioningSegmentSize + kMetadataSizeSegmentSize;

template <class T, class U, std::size_t N, std::size_t M>
constexpr bool starts_with(const cpp20::span<T, N>& data, const cpp20::span<U, M>& prefix) {
  return data.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), data.begin());
}

template <class T, class U, std::size_t N, std::size_t M>
void copy_into(cpp20::span<U, M>& to, const cpp20::span<T, N>& from) {
  ZX_ASSERT_MSG(from.size() <= to.size(), "from size (%zu) < to size (%zu)", from.size(),
                to.size());
  std::copy(from.begin(), from.end(), to.begin());
}

template <class T>
void advance_by(cpp20::span<T>& buf, size_t len) {
  ZX_ASSERT_MSG(buf.size() >= len, "buf size (%zu) < len (%zu)", buf.size(), len);
  buf = buf.subspan(len);
}

template <class T, class U, std::size_t N, std::size_t M>
void copy_into_and_advance_by(cpp20::span<U, M>& to, const cpp20::span<T, N>& from) {
  copy_into(to, from);
  advance_by(to, from.size());
}

bool consume_versioning_segment_unchecked(cpp20::span<uint8_t>& buf) {
  if (!starts_with(buf, kVersioningSegmentSpan)) {
    return false;
  }
  advance_by(buf, kVersioningSegmentSpan.size());
  return true;
}

uint16_t consume_meta_size_segment_unchecked(cpp20::span<uint8_t>& buf) {
  uint8_t meta_size[kMetadataSizeSize];
  cpp20::span<uint8_t, sizeof(meta_size)> meta_size_span(meta_size);
  copy_into(meta_size_span, buf.subspan(0, sizeof(meta_size)));
  advance_by(buf, kMetadataSizeSegmentSize);
  return *reinterpret_cast<uint16_t*>(meta_size);
}

void serialize_unchecked(cpp20::span<uint8_t>& buf, uint16_t meta_size,
                         const fidl::OutgoingMessage& msg) {
  copy_into_and_advance_by(
      buf, cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(&meta_size), sizeof(meta_size)));
  copy_into_and_advance_by(buf, kMetaSizeSegmentPaddingSpan);

  // This prelude is zeroed in preparation for the transition to use FIDL-at-rest as an encoding
  // scheme. Since FIDL-at-rest serializes a non-zero magic number within the first eight bytes
  // (see
  // https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0120_standalone_use_of_fidl_wire_format?hl=en#fidl_wire_format)
  // zero-ing them will let us signal the encoding scheme during the period of transition.
  copy_into_and_advance_by(buf, kVersioningSegmentSpan);

  for (uint32_t i = 0; i < msg.iovec_actual(); ++i) {
    copy_into_and_advance_by(
        buf, cpp20::span<const uint8_t>(static_cast<const uint8_t*>(msg.iovecs()[i].buffer),
                                        msg.iovecs()[i].capacity));
  }
}

std::optional<uint16_t> compute_and_validate_message_size(const fidl::OutgoingMessage& msg) {
  size_t total = 0;
  for (uint32_t i = 0; i < msg.iovec_actual(); ++i) {
    total += msg.iovecs()[i].capacity;
  }
  // Message must fit within 2 bytes.
  if (total > std::numeric_limits<uint16_t>::max()) {
    return std::nullopt;
  }

  return static_cast<uint16_t>(total);
}

bool can_serialize_into(const cpp20::span<uint8_t>& buf, uint16_t meta_size) {
  return static_cast<uint64_t>(meta_size) + static_cast<uint64_t>(kSumOfSegmentSizes) < buf.size();
}

}  // namespace

// Size occupied by the prelude bytes in a Tx message.
const uint32_t kTxUdpPreludeSize =
    fidl::MaxSizeInChannel<fsocket::wire::SendMsgMeta, fidl::MessageDirection::kSending>() +
    kSumOfSegmentSizes;

// Size occupied by the prelude bytes in an Rx message.
const uint32_t kRxUdpPreludeSize =
    fidl::MaxSizeInChannel<fsocket::wire::RecvMsgMeta, fidl::MessageDirection::kSending>() +
    kSumOfSegmentSizes;

DeserializeSendMsgMetaResult deserialize_send_msg_meta(Buffer buf) {
  DeserializeSendMsgMetaResult res;
  if (buf.buf == nullptr) {
    res.err = DeserializeSendMsgMetaErrorInputBufferNull;
    return res;
  }
  if (buf.buf_size < kSumOfSegmentSizes) {
    res.err = DeserializeSendMsgMetaErrorInputBufferTooSmall;
    return res;
  }
  cpp20::span<uint8_t> span{buf.buf, buf.buf_size};
  uint16_t meta_size = consume_meta_size_segment_unchecked(span);
  if (!consume_versioning_segment_unchecked(span)) {
    res.err = DeserializeSendMsgMetaErrorNonZeroPrelude;
    return res;
  }
  if (span.size() < meta_size) {
    res.err = DeserializeSendMsgMetaErrorInputBufferTooSmall;
    return res;
  }
  fidl::unstable::DecodedMessage<fsocket::wire::SendMsgMeta> decoded(
      fidl::internal::WireFormatVersion::kV2, span.begin(), static_cast<uint32_t>(meta_size));

  if (!decoded.ok()) {
    res.err = DeserializeSendMsgMetaErrorFailedToDecode;
    return res;
  }

  fsocket::wire::SendMsgMeta& meta = *decoded.PrimaryObject();

  if (meta.has_to()) {
    res.has_addr = true;
    fnet::wire::SocketAddress& sockaddr = meta.to();
    switch (sockaddr.Which()) {
      case fnet::wire::SocketAddress::Tag::kIpv4: {
        const fnet::wire::Ipv4SocketAddress& ipv4 = sockaddr.ipv4();
        res.port = ipv4.port;
        res.to_addr.addr_type = IpAddrType::Ipv4;
        std::copy(ipv4.address.addr.begin(), ipv4.address.addr.end(), res.to_addr.addr);
        break;
      }
      case fnet::wire::SocketAddress::Tag::kIpv6: {
        const fnet::wire::Ipv6SocketAddress& ipv6 = sockaddr.ipv6();
        res.port = ipv6.port;
        res.to_addr.addr_type = IpAddrType::Ipv6;
        std::copy(ipv6.address.addr.begin(), ipv6.address.addr.end(), res.to_addr.addr);
        break;
      }
    }
  } else {
    res.has_addr = false;
  }

  res.err = DeserializeSendMsgMetaErrorNone;
  return res;
}

SerializeSendMsgMetaError serialize_send_msg_meta(fsocket::wire::SendMsgMeta& meta,
                                                  cpp20::span<uint8_t> out_buf) {
  fidl::unstable::OwnedEncodedMessage<fsocket::wire::SendMsgMeta> encoded(
      fidl::internal::WireFormatVersion::kV2, &meta);
  if (!encoded.ok()) {
    return SerializeSendMsgMetaErrorFailedToEncode;
  }

  fidl::OutgoingMessage& outgoing_meta = encoded.GetOutgoingMessage();
  std::optional meta_size_validated = compute_and_validate_message_size(outgoing_meta);
  if (!meta_size_validated.has_value() ||
      !can_serialize_into(out_buf, meta_size_validated.value())) {
    return SerializeSendMsgMetaErrorOutputBufferTooSmall;
  }
  serialize_unchecked(out_buf, meta_size_validated.value(), outgoing_meta);
  return SerializeSendMsgMetaErrorNone;
}

SerializeSendMsgMetaError serialize_send_msg_meta_from_addr(IpAddress addr, uint16_t port,
                                                            Buffer out_buf) {
  fidl::Arena<
      fidl::MaxSizeInChannel<fsocket::wire::SendMsgMeta, fidl::MessageDirection::kSending>()>
      alloc;
  fidl::WireTableBuilder<fsocket::wire::SendMsgMeta> meta_builder =
      fsocket::wire::SendMsgMeta::Builder(alloc);

  fnet::wire::SocketAddress socket_addr;
  fnet::wire::Ipv4SocketAddress ipv4_socket_addr;
  fnet::wire::Ipv6SocketAddress ipv6_socket_addr;
  switch (addr.addr_type) {
    case IpAddrType::Ipv4: {
      cpp20::span<const uint8_t> from_addr_span(addr.addr, sizeof(ipv4_socket_addr.address.addr));
      cpp20::span<uint8_t> to_addr(ipv4_socket_addr.address.addr.data(),
                                   sizeof(ipv4_socket_addr.address.addr));
      copy_into(to_addr, from_addr_span);
      ipv4_socket_addr.port = port;
      socket_addr = fnet::wire::SocketAddress::WithIpv4(alloc, ipv4_socket_addr);
    } break;
    case IpAddrType::Ipv6: {
      cpp20::span<const uint8_t> from_addr_span(addr.addr, sizeof(ipv6_socket_addr.address.addr));
      cpp20::span<uint8_t> to_addr(ipv6_socket_addr.address.addr.data(),
                                   sizeof(ipv6_socket_addr.address.addr));
      copy_into(to_addr, from_addr_span);
      ipv6_socket_addr.port = port;
      socket_addr = fnet::wire::SocketAddress::WithIpv6(alloc, ipv6_socket_addr);
    } break;
  }
  meta_builder.to(socket_addr);
  fsocket::wire::SendMsgMeta meta = meta_builder.Build();
  if (out_buf.buf == nullptr) {
    return SerializeSendMsgMetaErrorOutputBufferNull;
  }
  return serialize_send_msg_meta(meta, cpp20::span<uint8_t>(out_buf.buf, out_buf.buf_size));
}

fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta> deserialize_recv_msg_meta(
    cpp20::span<uint8_t> buf) {
  if (buf.size() < kSumOfSegmentSizes) {
    return fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta>(nullptr, 0);
  }
  uint16_t meta_size = consume_meta_size_segment_unchecked(buf);
  if (!consume_versioning_segment_unchecked(buf)) {
    return fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta>(nullptr, 0);
  }

  if (meta_size > buf.size()) {
    return fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta>(nullptr, 0);
  }

  return fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta>(
      fidl::internal::WireFormatVersion::kV2, buf.data(), static_cast<uint32_t>(meta_size));
}

SerializeRecvMsgMetaError serialize_recv_msg_meta(const RecvMsgMeta* meta_, ConstBuffer from_addr,
                                                  Buffer out_buf) {
  fidl::Arena<
      fidl::MaxSizeInChannel<fsocket::wire::RecvMsgMeta, fidl::MessageDirection::kSending>()>
      alloc;
  fidl::WireTableBuilder<fsocket::wire::RecvMsgMeta> meta_builder =
      fsocket::wire::RecvMsgMeta::Builder(alloc);

  fnet::wire::SocketAddress socket_addr;
  fnet::wire::Ipv4SocketAddress ipv4_socket_addr;
  fnet::wire::Ipv6SocketAddress ipv6_socket_addr;
  const RecvMsgMeta& meta = *meta_;
  switch (meta.from_addr_type) {
    case IpAddrType::Ipv4: {
      if (from_addr.buf == nullptr) {
        return SerializeRecvMsgMetaErrorFromAddrBufferNull;
      }
      cpp20::span<const uint8_t> from_addr_span(from_addr.buf, from_addr.buf_size);
      cpp20::span<uint8_t> to_addr(ipv4_socket_addr.address.addr.data(),
                                   sizeof(ipv4_socket_addr.address.addr));
      if (from_addr_span.size() != to_addr.size()) {
        return SerializeRecvMsgMetaErrorFromAddrBufferTooSmall;
      }
      copy_into(to_addr, from_addr_span);
      ipv4_socket_addr.port = meta.port;
      socket_addr = fnet::wire::SocketAddress::WithIpv4(alloc, ipv4_socket_addr);
    } break;
    case IpAddrType::Ipv6: {
      if (from_addr.buf == nullptr) {
        return SerializeRecvMsgMetaErrorFromAddrBufferNull;
      }
      cpp20::span<const uint8_t> from_addr_span(from_addr.buf, from_addr.buf_size);
      cpp20::span<uint8_t> to_addr(ipv6_socket_addr.address.addr.data(),
                                   sizeof(ipv6_socket_addr.address.addr));
      if (from_addr_span.size() != to_addr.size()) {
        return SerializeRecvMsgMetaErrorFromAddrBufferTooSmall;
      }
      copy_into(to_addr, from_addr_span);
      ipv6_socket_addr.port = meta.port;
      socket_addr = fnet::wire::SocketAddress::WithIpv6(alloc, ipv6_socket_addr);
    } break;
  }
  meta_builder.from(socket_addr);

  fidl::WireTableBuilder<fsocket::wire::NetworkSocketRecvControlData> net_control_builder =
      fsocket::wire::NetworkSocketRecvControlData::Builder(alloc);
  bool net_control_set = false;

  {
    fidl::WireTableBuilder<fsocket::wire::IpRecvControlData> ip_control_builder =
        fsocket::wire::IpRecvControlData::Builder(alloc);
    bool ip_control_set = false;
    if (meta.cmsg_set.has_ip_tos) {
      ip_control_set = true;
      ip_control_builder.tos(meta.cmsg_set.ip_tos);
    }
    if (meta.cmsg_set.has_ip_ttl) {
      ip_control_set = true;
      ip_control_builder.ttl(meta.cmsg_set.ip_ttl);
    }
    if (ip_control_set) {
      net_control_set = true;
      net_control_builder.ip(ip_control_builder.Build());
    }
  }

  {
    fidl::WireTableBuilder<fsocket::wire::Ipv6RecvControlData> ipv6_control_builder =
        fsocket::wire::Ipv6RecvControlData::Builder(alloc);
    bool ipv6_control_set = false;
    if (meta.cmsg_set.has_ipv6_pktinfo) {
      const Ipv6PktInfo& pktinfo = meta.cmsg_set.ipv6_pktinfo;
      fuchsia_posix_socket::wire::Ipv6PktInfoRecvControlData fidl_pktinfo = {
          .iface = pktinfo.if_index,
      };
      const cpp20::span<const uint8_t> from_addr(pktinfo.addr);
      cpp20::span<uint8_t> to_addr(fidl_pktinfo.header_destination_addr.addr.data(),
                                   decltype(fidl_pktinfo.header_destination_addr.addr)::size());
      copy_into(to_addr, from_addr);
      ipv6_control_set = true;
      ipv6_control_builder.pktinfo(fidl_pktinfo);
    }
    if (meta.cmsg_set.has_ipv6_hoplimit) {
      ipv6_control_set = true;
      ipv6_control_builder.hoplimit(meta.cmsg_set.ipv6_hoplimit);
    }
    if (meta.cmsg_set.has_ipv6_tclass) {
      ipv6_control_set = true;
      ipv6_control_builder.tclass(meta.cmsg_set.ipv6_tclass);
    }
    if (ipv6_control_set) {
      net_control_set = true;
      net_control_builder.ipv6(ipv6_control_builder.Build());
    }
  }

  {
    fidl::WireTableBuilder<fsocket::wire::SocketRecvControlData> sock_control_builder =
        fsocket::wire::SocketRecvControlData::Builder(alloc);
    bool sock_control_set = false;
    if (meta.cmsg_set.has_timestamp_nanos) {
      sock_control_set = true;
      sock_control_builder.timestamp(fsocket::wire::Timestamp{
          .nanoseconds = meta.cmsg_set.timestamp_nanos,
      });
    }
    if (sock_control_set) {
      net_control_set = true;
      net_control_builder.socket(sock_control_builder.Build());
    }
  }

  if (net_control_set) {
    fidl::WireTableBuilder<fsocket::wire::DatagramSocketRecvControlData> datagram_control_builder =
        fsocket::wire::DatagramSocketRecvControlData::Builder(alloc);
    datagram_control_builder.network(net_control_builder.Build());
    meta_builder.control(datagram_control_builder.Build());
  }

  meta_builder.payload_len(meta.payload_size);

  fsocket::wire::RecvMsgMeta fsocket_meta = meta_builder.Build();

  fidl::unstable::OwnedEncodedMessage<fsocket::wire::RecvMsgMeta> encoded(
      fidl::internal::WireFormatVersion::kV2, &fsocket_meta);
  if (!encoded.ok()) {
    return SerializeRecvMsgMetaErrorFailedToEncode;
  }

  if (out_buf.buf == nullptr) {
    return SerializeRecvMsgMetaErrorOutputBufferNull;
  }

  cpp20::span<uint8_t> outbuf{out_buf.buf, out_buf.buf_size};

  fidl::OutgoingMessage& outgoing_meta = encoded.GetOutgoingMessage();
  std::optional meta_size_validated = compute_and_validate_message_size(outgoing_meta);
  if (!meta_size_validated.has_value() ||
      !can_serialize_into(outbuf, meta_size_validated.value())) {
    return SerializeRecvMsgMetaErrorOutputBufferTooSmall;
  }
  serialize_unchecked(outbuf, meta_size_validated.value(), outgoing_meta);
  return SerializeRecvMsgMetaErrorNone;
}
