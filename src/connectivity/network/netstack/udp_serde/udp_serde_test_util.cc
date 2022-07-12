// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "udp_serde_test_util.h"

namespace fnet = fuchsia_net;

namespace {
constexpr uint16_t kPort = 80;
constexpr fidl::Array<uint8_t, kIPv4AddrLen> kIPv4Addr = {0x1, 0x2, 0x3, 0x4};
constexpr fidl::Array<uint8_t, kIPv6AddrLen> kIPv6Addr = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                                                          0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
constexpr size_t kPayloadSize = 41;
constexpr int64_t kTimestampNanos = 42;
constexpr uint8_t kIpTos = 43;
constexpr uint8_t kIpTtl = 44;
constexpr uint8_t kIpv6Tclass = 45;
constexpr uint8_t kIpv6Hoplimit = 46;
constexpr uint8_t kIpv6PktInfoIfIdx = 47;
}  // namespace

std::ostream& operator<<(std::ostream& out, const span& span) {
  out << '[';
  const std::ios_base::fmtflags flags = out.flags();
  out.flags(std::ios::hex | std::ios::showbase);
  for (auto it = span.begin(); it != span.end(); ++it) {
    if (it != span.begin()) {
      out << ", ";
    }
    out << static_cast<unsigned int>(*it);
  }
  out.flags(flags);
  out << ']';
  return out;
}

IpAddrType AddrKind::ToAddrType() const {
  switch (kind_) {
    case Kind::V4:
      return IpAddrType::Ipv4;
    case Kind::V6:
      return IpAddrType::Ipv6;
  }
}

fsocket::wire::SendMsgMeta TestSendMsgMeta::Get(fidl::Arena<512>& alloc, bool with_data) const {
  fidl::WireTableBuilder<fsocket::wire::SendMsgMeta> meta_builder =
      fsocket::wire::SendMsgMeta::Builder(alloc);
  if (with_data) {
    fnet::wire::SocketAddress socket_addr;
    switch (kind_.GetKind()) {
      case AddrKind::Kind::V4: {
        fnet::wire::Ipv4Address ipv4_addr;
        ipv4_addr.addr = kIPv4Addr;
        fnet::wire::Ipv4SocketAddress ipv4_socket_addr;
        ipv4_socket_addr.address = ipv4_addr;
        ipv4_socket_addr.port = kPort;
        socket_addr = fnet::wire::SocketAddress::WithIpv4(alloc, ipv4_socket_addr);
      } break;
      case AddrKind::Kind::V6: {
        fnet::wire::Ipv6Address ipv6_addr;
        ipv6_addr.addr = kIPv6Addr;
        fnet::wire::Ipv6SocketAddress ipv6_socket_addr;
        ipv6_socket_addr.address = ipv6_addr;
        ipv6_socket_addr.port = kPort;
        socket_addr = fnet::wire::SocketAddress::WithIpv6(alloc, ipv6_socket_addr);
      }
    }
    meta_builder.to(socket_addr);
  }
  return meta_builder.Build();
}

const uint8_t* TestSendMsgMeta::Addr() const {
  switch (kind_.GetKind()) {
    case AddrKind::Kind::V4:
      return kIPv4Addr.data();
    case AddrKind::Kind::V6:
      return kIPv6Addr.data();
  }
}

size_t TestSendMsgMeta::AddrLen() const { return kind_.Len(); }

IpAddrType TestSendMsgMeta::AddrType() const { return kind_.ToAddrType(); }

uint16_t TestSendMsgMeta::Port() const { return kPort; }

std::pair<RecvMsgMeta, ConstBuffer> GetTestRecvMsgMeta(AddrKind::Kind kind, bool with_data) {
  RecvMsgMeta meta = {
      .cmsg_set =
          {
              .has_ip_tos = false,
              .has_ip_ttl = false,
              .has_ipv6_tclass = false,
              .has_ipv6_hoplimit = false,
              .has_timestamp_nanos = false,
              .has_ipv6_pktinfo = false,
          },
      .payload_size = kPayloadSize,
      .port = kPort,
  };

  if (with_data) {
    meta.cmsg_set.has_timestamp_nanos = true;
    meta.cmsg_set.timestamp_nanos = kTimestampNanos;
  }

  switch (kind) {
    case AddrKind::Kind::V4:
      if (with_data) {
        meta.from_addr_type = IpAddrType::Ipv4;
        meta.cmsg_set.has_ip_tos = true;
        meta.cmsg_set.ip_tos = kIpTos;
        meta.cmsg_set.has_ip_ttl = true;
        meta.cmsg_set.ip_ttl = kIpTtl;
      }
      meta.from_addr_type = IpAddrType::Ipv4;
      return {
          meta,
          ConstBuffer{
              .buf = kIPv4Addr.data(),
              .buf_size = kIPv4Addr.size(),
          },
      };
    case AddrKind::Kind::V6: {
      const ConstBuffer kIPv6AddrBuf = {
          .buf = kIPv6Addr.data(),
          .buf_size = kIPv6Addr.size(),
      };
      if (with_data) {
        meta.from_addr_type = IpAddrType::Ipv6;
        meta.cmsg_set.has_ipv6_tclass = true;
        meta.cmsg_set.ipv6_tclass = kIpv6Tclass;
        meta.cmsg_set.has_ipv6_hoplimit = true;
        meta.cmsg_set.ipv6_hoplimit = kIpv6Hoplimit;
        meta.cmsg_set.has_ipv6_pktinfo = true;
        meta.cmsg_set.ipv6_pktinfo = {
            .if_index = kIpv6PktInfoIfIdx,
        };
        memcpy(meta.cmsg_set.ipv6_pktinfo.addr, kIPv6AddrBuf.buf, kIPv6AddrBuf.buf_size);
      }
      meta.from_addr_type = IpAddrType::Ipv6;
      return {
          meta,
          kIPv6AddrBuf,
      };
    } break;
  }
}
