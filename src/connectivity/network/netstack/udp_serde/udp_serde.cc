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

constexpr uint8_t kMetadataSizeSegmentSize = 8;
constexpr uint8_t kMetadataSizeSize = sizeof(uint16_t);
constexpr uint8_t kMetadataSizeSegmentPaddingSize = kMetadataSizeSegmentSize - kMetadataSizeSize;
constexpr uint8_t kMetadataSizeSegmentPadding[kMetadataSizeSegmentPaddingSize] = {0};
constexpr cpp20::span<const uint8_t> kMetaSizeSegmentPaddingSpan(kMetadataSizeSegmentPadding,
                                                                 kMetadataSizeSegmentPaddingSize);

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
  return static_cast<uint64_t>(meta_size) + static_cast<uint64_t>(kMetadataSizeSegmentSize) <
         buf.size();
}

// Copies the address in `src_addr` into the provided SocketAddress data structures.
//
// Returns a bool indicating whether the operation succeeded:
//  - If the size of the provided `src_addr` doesn't match the size expected for an
//    IP address of the type `meta.addr_type`, returns false.
//  - Else, returns true.
template <typename Meta, size_t AllocSize>
bool copy_into_sockaddr(const Meta& meta, const cpp20::span<const uint8_t>& src_addr,
                        fidl::Arena<AllocSize>& alloc, fnet::wire::SocketAddress& dst_sockaddr,
                        fnet::wire::Ipv4SocketAddress& dst_ipv4_sockaddr,
                        fnet::wire::Ipv6SocketAddress& dst_ipv6_sockaddr) {
  switch (meta.addr_type) {
    case IpAddrType::Ipv4: {
      cpp20::span<uint8_t> dst_addr(dst_ipv4_sockaddr.address.addr.data(),
                                    sizeof(dst_ipv4_sockaddr.address.addr));
      if (src_addr.size() != dst_addr.size()) {
        return false;
      }
      copy_into(dst_addr, src_addr);
      dst_ipv4_sockaddr.port = meta.port;
      dst_sockaddr = fnet::wire::SocketAddress::WithIpv4(alloc, dst_ipv4_sockaddr);
    } break;
    case IpAddrType::Ipv6: {
      cpp20::span<uint8_t> dst_addr(dst_ipv6_sockaddr.address.addr.data(),
                                    sizeof(dst_ipv6_sockaddr.address.addr));
      if (src_addr.size() != dst_addr.size()) {
        return false;
      }
      copy_into(dst_addr, src_addr);
      dst_ipv6_sockaddr.port = meta.port;
      dst_ipv6_sockaddr.zone_index = meta.zone_index;
      dst_sockaddr = fnet::wire::SocketAddress::WithIpv6(alloc, dst_ipv6_sockaddr);
    } break;
  }
  return true;
}

template <typename T>
void copy_from_fidl_sockaddr(T& dest, const fnet::wire::SocketAddress& sockaddr) {
  dest.has_addr = true;
  switch (sockaddr.Which()) {
    case fnet::wire::SocketAddress::Tag::kIpv4: {
      const fnet::wire::Ipv4SocketAddress& ipv4 = sockaddr.ipv4();
      dest.port = ipv4.port;
      dest.addr.addr_type = IpAddrType::Ipv4;
      static_assert(sizeof(dest.addr.addr) >= sizeof(ipv4.address.addr));
      memcpy(dest.addr.addr, ipv4.address.addr.data(), sizeof(ipv4.address.addr));
      break;
    }
    case fnet::wire::SocketAddress::Tag::kIpv6: {
      const fnet::wire::Ipv6SocketAddress& ipv6 = sockaddr.ipv6();
      dest.port = ipv6.port;
      dest.addr.addr_type = IpAddrType::Ipv6;
      dest.zone_index = ipv6.zone_index;
      static_assert(sizeof(dest.addr.addr) == sizeof(ipv6.address.addr));
      memcpy(dest.addr.addr, ipv6.address.addr.data(), sizeof(ipv6.address.addr));
      break;
    }
  }
}

}  // namespace

// Size occupied by the prelude bytes in a Tx message.
const uint32_t kTxUdpPreludeSize =
    fidl::MaxSizeInChannel<fsocket::wire::SendMsgMeta, fidl::MessageDirection::kSending>() +
    kMetadataSizeSegmentSize;

// Size occupied by the prelude bytes in an Rx message.
const uint32_t kRxUdpPreludeSize =
    fidl::MaxSizeInChannel<fsocket::wire::RecvMsgMeta, fidl::MessageDirection::kSending>() +
    kMetadataSizeSegmentSize;

DeserializeSendMsgMetaResult deserialize_send_msg_meta(Buffer buf) {
  DeserializeSendMsgMetaResult res = {};
  if (buf.buf == nullptr) {
    res.err = DeserializeSendMsgMetaErrorInputBufferNull;
    return res;
  }
  if (buf.buf_size < kMetadataSizeSegmentSize) {
    res.err = DeserializeSendMsgMetaErrorInputBufferTooSmall;
    return res;
  }
  cpp20::span<uint8_t> span{buf.buf, buf.buf_size};
  uint16_t meta_size = consume_meta_size_segment_unchecked(span);
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
    copy_from_fidl_sockaddr(res, meta.to());
  }

  if (meta.has_control()) {
    fsocket::wire::DatagramSocketSendControlData& control = meta.control();
    if (control.has_network()) {
      fsocket::wire::NetworkSocketSendControlData& network = control.network();
      if (network.has_ip()) {
        fsocket::wire::IpSendControlData& ip = network.ip();
        if (ip.has_ttl()) {
          res.cmsg_set.has_ip_ttl = true;
          res.cmsg_set.ip_ttl = ip.ttl();
        }
      }
      if (network.has_ipv6()) {
        fsocket::wire::Ipv6SendControlData& ipv6 = network.ipv6();
        if (ipv6.has_hoplimit()) {
          res.cmsg_set.has_ipv6_hoplimit = true;
          res.cmsg_set.ipv6_hoplimit = ipv6.hoplimit();
        }
        if (ipv6.has_pktinfo()) {
          res.cmsg_set.has_ipv6_pktinfo = true;
          fsocket::wire::Ipv6PktInfoSendControlData& pktinfo = ipv6.pktinfo();
          res.cmsg_set.ipv6_pktinfo.if_index = pktinfo.iface;
          static_assert(sizeof(res.cmsg_set.ipv6_pktinfo.addr) == sizeof(pktinfo.local_addr.addr));
          memcpy(res.cmsg_set.ipv6_pktinfo.addr, pktinfo.local_addr.addr.data(),
                 sizeof(pktinfo.local_addr.addr));
        }
      }
    }
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

SerializeSendMsgMetaError serialize_send_msg_meta(const SendMsgMeta* meta_, ConstBuffer addr,
                                                  Buffer out_buf) {
  fidl::Arena<
      fidl::MaxSizeInChannel<fsocket::wire::SendMsgMeta, fidl::MessageDirection::kSending>()>
      alloc;
  fidl::WireTableBuilder<fsocket::wire::SendMsgMeta> meta_builder =
      fsocket::wire::SendMsgMeta::Builder(alloc);

  if (addr.buf == nullptr) {
    return SerializeSendMsgMetaErrorAddrBufferNull;
  }
  fnet::wire::SocketAddress socket_addr;
  fnet::wire::Ipv4SocketAddress ipv4_socket_addr;
  fnet::wire::Ipv6SocketAddress ipv6_socket_addr;
  const SendMsgMeta& meta = *meta_;
  if (!copy_into_sockaddr(meta, cpp20::span<const uint8_t>(addr.buf, addr.buf_size), alloc,
                          socket_addr, ipv4_socket_addr, ipv6_socket_addr)) {
    return SerializeSendMsgMetaErrorAddrBufferSizeMismatch;
  }
  meta_builder.to(socket_addr);

  const SendAndRecvCmsgSet& cmsg_set = meta.cmsg_set;
  fidl::WireTableBuilder<fsocket::wire::NetworkSocketSendControlData> net_control_builder =
      fsocket::wire::NetworkSocketSendControlData::Builder(alloc);
  bool net_control_set = false;

  {
    fidl::WireTableBuilder<fsocket::wire::IpSendControlData> ip_control_builder =
        fsocket::wire::IpSendControlData::Builder(alloc);
    bool ip_control_set = false;
    if (cmsg_set.has_ip_ttl) {
      ip_control_set = true;
      ip_control_builder.ttl(cmsg_set.ip_ttl);
    }
    if (ip_control_set) {
      net_control_set = true;
      net_control_builder.ip(ip_control_builder.Build());
    }
  }

  {
    fidl::WireTableBuilder<fsocket::wire::Ipv6SendControlData> ipv6_control_builder =
        fsocket::wire::Ipv6SendControlData::Builder(alloc);
    bool ipv6_control_set = false;
    if (cmsg_set.has_ipv6_pktinfo) {
      const Ipv6PktInfo& pktinfo = cmsg_set.ipv6_pktinfo;
      fuchsia_posix_socket::wire::Ipv6PktInfoSendControlData fidl_pktinfo = {
          .iface = pktinfo.if_index,
      };
      static_assert(sizeof(pktinfo.addr) == sizeof(fidl_pktinfo.local_addr.addr));
      memcpy(fidl_pktinfo.local_addr.addr.data(), pktinfo.addr,
             sizeof(fidl_pktinfo.local_addr.addr));
      ipv6_control_set = true;
      ipv6_control_builder.pktinfo(fidl_pktinfo);
    }
    if (cmsg_set.has_ipv6_hoplimit) {
      ipv6_control_set = true;
      ipv6_control_builder.hoplimit(cmsg_set.ipv6_hoplimit);
    }
    if (ipv6_control_set) {
      net_control_set = true;
      net_control_builder.ipv6(ipv6_control_builder.Build());
    }
  }

  if (net_control_set) {
    fidl::WireTableBuilder<fsocket::wire::DatagramSocketSendControlData> datagram_control_builder =
        fsocket::wire::DatagramSocketSendControlData::Builder(alloc);
    datagram_control_builder.network(net_control_builder.Build());
    meta_builder.control(datagram_control_builder.Build());
  }

  fsocket::wire::SendMsgMeta fidl_meta = meta_builder.Build();
  if (out_buf.buf == nullptr) {
    return SerializeSendMsgMetaErrorOutputBufferNull;
  }
  return serialize_send_msg_meta(fidl_meta, cpp20::span<uint8_t>(out_buf.buf, out_buf.buf_size));
}

DeserializeRecvMsgMetaResult deserialize_recv_msg_meta(Buffer buf) {
  DeserializeRecvMsgMetaResult res = {};
  if (buf.buf == nullptr) {
    res.err = DeserializeRecvMsgMetaErrorInputBufferNull;
    return res;
  }
  fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta> decoded_meta =
      deserialize_recv_msg_meta(cpp20::span<uint8_t>(buf.buf, buf.buf_size));
  if (!decoded_meta.ok()) {
    res.err = DeserializeRecvMsgMetaErrorUnspecifiedDecodingFailure;
    return res;
  }
  const fuchsia_posix_socket::wire::RecvMsgMeta& meta = *decoded_meta.PrimaryObject();

  if (meta.has_from()) {
    copy_from_fidl_sockaddr(res, meta.from());
  }

  if (meta.has_payload_len()) {
    res.payload_size = meta.payload_len();
  }

  if (meta.has_control()) {
    const fsocket::wire::DatagramSocketRecvControlData& control = meta.control();
    if (control.has_network()) {
      const fsocket::wire::NetworkSocketRecvControlData& network = control.network();
      if (network.has_socket()) {
        const fsocket::wire::SocketRecvControlData& socket = network.socket();
        if (socket.has_timestamp()) {
          const fsocket::wire::Timestamp& timestamp = socket.timestamp();
          res.cmsg_set.has_timestamp_nanos = true;
          res.cmsg_set.timestamp_nanos = timestamp.nanoseconds;
        }
      }
      if (network.has_ip()) {
        const fsocket::wire::IpRecvControlData& ip = network.ip();
        if (ip.has_ttl()) {
          res.cmsg_set.send_and_recv.has_ip_ttl = true;
          res.cmsg_set.send_and_recv.ip_ttl = ip.ttl();
        }
        if (ip.has_tos()) {
          res.cmsg_set.has_ip_tos = true;
          res.cmsg_set.ip_tos = ip.tos();
        }
      }
      if (network.has_ipv6()) {
        const fsocket::wire::Ipv6RecvControlData& ipv6 = network.ipv6();
        if (ipv6.has_hoplimit()) {
          res.cmsg_set.send_and_recv.has_ipv6_hoplimit = true;
          res.cmsg_set.send_and_recv.ipv6_hoplimit = ipv6.hoplimit();
        }
        if (ipv6.has_pktinfo()) {
          res.cmsg_set.send_and_recv.has_ipv6_pktinfo = true;
          const fsocket::wire::Ipv6PktInfoRecvControlData& pktinfo = ipv6.pktinfo();
          res.cmsg_set.send_and_recv.ipv6_pktinfo.if_index = pktinfo.iface;
          static_assert(sizeof(res.cmsg_set.send_and_recv.ipv6_pktinfo.addr) ==
                        sizeof(pktinfo.header_destination_addr.addr));
          memcpy(res.cmsg_set.send_and_recv.ipv6_pktinfo.addr,
                 pktinfo.header_destination_addr.addr.data(),
                 sizeof(pktinfo.header_destination_addr.addr));
        }
        if (ipv6.has_tclass()) {
          res.cmsg_set.has_ipv6_tclass = true;
          res.cmsg_set.ipv6_tclass = ipv6.tclass();
        }
      }
    }
  }
  return res;
}

fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta> deserialize_recv_msg_meta(
    cpp20::span<uint8_t> buf) {
  if (buf.size() < kMetadataSizeSegmentSize) {
    return fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta>(nullptr, 0);
  }
  uint16_t meta_size = consume_meta_size_segment_unchecked(buf);
  if (meta_size > buf.size()) {
    return fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta>(nullptr, 0);
  }

  return fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta>(
      fidl::internal::WireFormatVersion::kV2, buf.data(), static_cast<uint32_t>(meta_size));
}

SerializeRecvMsgMetaError serialize_recv_msg_meta(const RecvMsgMeta* meta_, ConstBuffer addr,
                                                  Buffer out_buf) {
  fidl::Arena<
      fidl::MaxSizeInChannel<fsocket::wire::RecvMsgMeta, fidl::MessageDirection::kSending>()>
      alloc;
  fidl::WireTableBuilder<fsocket::wire::RecvMsgMeta> meta_builder =
      fsocket::wire::RecvMsgMeta::Builder(alloc);

  if (addr.buf == nullptr) {
    return SerializeRecvMsgMetaErrorFromAddrBufferNull;
  }
  fnet::wire::SocketAddress socket_addr;
  fnet::wire::Ipv4SocketAddress ipv4_socket_addr;
  fnet::wire::Ipv6SocketAddress ipv6_socket_addr;
  const RecvMsgMeta& meta = *meta_;
  if (!copy_into_sockaddr(meta, cpp20::span<const uint8_t>(addr.buf, addr.buf_size), alloc,
                          socket_addr, ipv4_socket_addr, ipv6_socket_addr)) {
    return SerializeRecvMsgMetaErrorFromAddrBufferTooSmall;
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
    if (meta.cmsg_set.send_and_recv.has_ip_ttl) {
      ip_control_set = true;
      ip_control_builder.ttl(meta.cmsg_set.send_and_recv.ip_ttl);
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
    if (meta.cmsg_set.send_and_recv.has_ipv6_pktinfo) {
      const Ipv6PktInfo& pktinfo = meta.cmsg_set.send_and_recv.ipv6_pktinfo;
      fuchsia_posix_socket::wire::Ipv6PktInfoRecvControlData fidl_pktinfo = {
          .iface = pktinfo.if_index,
      };
      const cpp20::span<const uint8_t> src(pktinfo.addr);
      cpp20::span<uint8_t> dst(fidl_pktinfo.header_destination_addr.addr.data(),
                               decltype(fidl_pktinfo.header_destination_addr.addr)::size());
      copy_into(dst, src);
      ipv6_control_set = true;
      ipv6_control_builder.pktinfo(fidl_pktinfo);
    }
    if (meta.cmsg_set.send_and_recv.has_ipv6_hoplimit) {
      ipv6_control_set = true;
      ipv6_control_builder.hoplimit(meta.cmsg_set.send_and_recv.ipv6_hoplimit);
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
