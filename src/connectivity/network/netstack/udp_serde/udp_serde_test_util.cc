// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "udp_serde_test_util.h"

namespace fnet = fuchsia_net;

namespace {
constexpr uint16_t kPort = 80;
constexpr size_t kPayloadSize = 41;
constexpr int64_t kTimestampNanos = 42;
constexpr uint8_t kIpTos = 43;
constexpr uint8_t kIpv6Tclass = 45;
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

fsocket::wire::SendMsgMeta TestSendMsgMeta::GetFidl(fidl::Arena<512>& alloc, bool with_data) const {
  fidl::WireTableBuilder<fsocket::wire::SendMsgMeta> meta_builder =
      fsocket::wire::SendMsgMeta::Builder(alloc);
  if (with_data) {
    fnet::wire::SocketAddress socket_addr;
    fidl::WireTableBuilder<fsocket::wire::DatagramSocketSendControlData> dgram_builder =
        fsocket::wire::DatagramSocketSendControlData::Builder(alloc);
    fidl::WireTableBuilder<fsocket::wire::NetworkSocketSendControlData> network_builder =
        fsocket::wire::NetworkSocketSendControlData::Builder(alloc);
    switch (kind_.GetKind()) {
      case AddrKind::Kind::V4: {
        fnet::wire::Ipv4Address ipv4_addr;
        ipv4_addr.addr = kIPv4Addr;
        fnet::wire::Ipv4SocketAddress ipv4_socket_addr;
        ipv4_socket_addr.address = ipv4_addr;
        ipv4_socket_addr.port = kPort;
        socket_addr = fnet::wire::SocketAddress::WithIpv4(alloc, ipv4_socket_addr);

        fidl::WireTableBuilder<fsocket::wire::IpSendControlData> ip_builder =
            fsocket::wire::IpSendControlData::Builder(alloc);
        ip_builder.ttl(kIpTtl);
        network_builder.ip(ip_builder.Build());
      } break;
      case AddrKind::Kind::V6: {
        fnet::wire::Ipv6Address ipv6_addr;
        ipv6_addr.addr = kIPv6Addr;
        fnet::wire::Ipv6SocketAddress ipv6_socket_addr;
        ipv6_socket_addr.address = ipv6_addr;
        ipv6_socket_addr.port = kPort;
        socket_addr = fnet::wire::SocketAddress::WithIpv6(alloc, ipv6_socket_addr);

        fidl::WireTableBuilder<fsocket::wire::Ipv6SendControlData> ip_builder =
            fsocket::wire::Ipv6SendControlData::Builder(alloc);
        ip_builder.hoplimit(kIpv6Hoplimit);
        fsocket::wire::Ipv6PktInfoSendControlData pktinfo = {
            .iface = kIpv6PktInfoIfIdx,
            .local_addr = ipv6_addr,
        };
        ip_builder.pktinfo(pktinfo);
        network_builder.ipv6(ip_builder.Build());
      }
    }
    dgram_builder.network(network_builder.Build());
    meta_builder.control(dgram_builder.Build());
    meta_builder.to(socket_addr);
  }
  return meta_builder.Build();
}

SendMsgMeta TestSendMsgMeta::GetCStruct() const {
  return {
      .cmsg_set = CmsgSet(),
      .addr_type = AddrType(),
      .port = Port(),
  };
}

const uint8_t* TestSendMsgMeta::Addr() const {
  switch (kind_.GetKind()) {
    case AddrKind::Kind::V4:
      return kIPv4Addr.data();
    case AddrKind::Kind::V6:
      return kIPv6Addr.data();
  }
}

SendAndRecvCmsgSet TestSendMsgMeta::CmsgSet() const {
  SendAndRecvCmsgSet cmsg_set = {
      .has_ip_ttl = false,
      .has_ipv6_hoplimit = false,
      .has_ipv6_pktinfo = false,
  };

  switch (kind_.GetKind()) {
    case AddrKind::Kind::V4:
      cmsg_set.has_ip_ttl = true;
      cmsg_set.ip_ttl = kIpTtl;
      break;
    case AddrKind::Kind::V6:
      cmsg_set.has_ipv6_hoplimit = true;
      cmsg_set.ipv6_hoplimit = kIpv6Hoplimit;

      cmsg_set.has_ipv6_pktinfo = true;
      cmsg_set.ipv6_pktinfo.if_index = kIpv6PktInfoIfIdx;
      memcpy(cmsg_set.ipv6_pktinfo.addr, kIPv6Addr.data(), sizeof(kIPv6Addr));
      break;
  }
  return cmsg_set;
}

size_t TestSendMsgMeta::AddrLen() const { return kind_.Len(); }

IpAddrType TestSendMsgMeta::AddrType() const { return kind_.ToAddrType(); }

uint16_t TestSendMsgMeta::Port() const { return kPort; }

std::pair<RecvMsgMeta, ConstBuffer> GetTestRecvMsgMeta(AddrKind::Kind kind, bool with_data) {
  RecvMsgMeta meta = {
      .cmsg_set =
          {
              .has_timestamp_nanos = false,
              .has_ip_tos = false,
              .has_ipv6_tclass = false,
              .send_and_recv =
                  {
                      .has_ip_ttl = false,
                      .has_ipv6_hoplimit = false,
                      .has_ipv6_pktinfo = false,
                  },
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
        meta.cmsg_set.send_and_recv.has_ip_ttl = true;
        meta.cmsg_set.send_and_recv.ip_ttl = kIpTtl;
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
        meta.cmsg_set.send_and_recv.has_ipv6_hoplimit = true;
        meta.cmsg_set.send_and_recv.ipv6_hoplimit = kIpv6Hoplimit;
        meta.cmsg_set.send_and_recv.has_ipv6_pktinfo = true;
        meta.cmsg_set.send_and_recv.ipv6_pktinfo = {
            .if_index = kIpv6PktInfoIfIdx,
        };
        memcpy(meta.cmsg_set.send_and_recv.ipv6_pktinfo.addr, kIPv6AddrBuf.buf,
               kIPv6AddrBuf.buf_size);
      }
      meta.from_addr_type = IpAddrType::Ipv6;
      return {
          meta,
          kIPv6AddrBuf,
      };
    } break;
  }
}
