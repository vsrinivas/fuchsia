// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.posix.socket.packet/cpp/wire.h>
#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <lib/zxio/cpp/cmsg.h>
#include <lib/zxio/cpp/dgram_cache.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>

namespace fsocket = fuchsia_posix_socket;
namespace fpacketsocket = fuchsia_posix_socket_packet;

using fuchsia_posix_socket::wire::CmsgRequests;

socklen_t FidlControlDataProcessor::Store(
    fsocket::wire::DatagramSocketRecvControlData const& control_data,
    const RequestedCmsgSet& requested) {
  socklen_t total = 0;
  if (control_data.has_network()) {
    total += Store(control_data.network(), requested);
  }
  return total;
}

socklen_t FidlControlDataProcessor::Store(
    fsocket::wire::NetworkSocketRecvControlData const& control_data,
    const RequestedCmsgSet& requested) {
  socklen_t total = 0;
  if (control_data.has_socket()) {
    total += Store(control_data.socket(), requested);
  }
  if (control_data.has_ip()) {
    total += Store(control_data.ip(), requested);
  }
  if (control_data.has_ipv6()) {
    total += Store(control_data.ipv6(), requested);
  }
  return total;
}

socklen_t FidlControlDataProcessor::Store(fpacketsocket::wire::RecvControlData const& control_data,
                                          const RequestedCmsgSet& requested) {
  socklen_t total = 0;
  if (control_data.has_socket()) {
    total += Store(control_data.socket(), requested);
  }
  return total;
}

socklen_t FidlControlDataProcessor::Store(fsocket::wire::SocketRecvControlData const& control_data,
                                          const RequestedCmsgSet& requested) {
  socklen_t total = 0;

  if (control_data.has_timestamp()) {
    const fsocket::wire::Timestamp& timestamp = control_data.timestamp();
    std::chrono::duration t = std::chrono::nanoseconds(timestamp.nanoseconds);
    std::chrono::seconds sec = std::chrono::duration_cast<std::chrono::seconds>(t);

    std::optional<fsocket::wire::TimestampOption> requested_ts = requested.so_timestamp();
    const fsocket::wire::TimestampOption& which_timestamp =
        requested_ts.has_value() ? requested_ts.value() : timestamp.requested;
    switch (which_timestamp) {
      case fsocket::wire::TimestampOption::kNanosecond: {
        const struct timespec ts = {
            .tv_sec = sec.count(),
            .tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(t - sec).count(),
        };
        total += StoreControlMessage(SOL_SOCKET, SO_TIMESTAMPNS, &ts, sizeof(ts));
      } break;
      case fsocket::wire::TimestampOption::kMicrosecond: {
        const struct timeval tv = {
            .tv_sec = sec.count(),
            .tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(t - sec).count(),
        };
        total += StoreControlMessage(SOL_SOCKET, SO_TIMESTAMP, &tv, sizeof(tv));
      } break;
      case fsocket::wire::TimestampOption::kDisabled:
        break;
    }
  }

  return total;
}

socklen_t FidlControlDataProcessor::Store(fsocket::wire::IpRecvControlData const& control_data,
                                          const RequestedCmsgSet& requested) {
  socklen_t total = 0;

  if (requested.ip_tos() && control_data.has_tos()) {
    const uint8_t tos = control_data.tos();
    total += StoreControlMessage(IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
  }

  if (requested.ip_ttl() && control_data.has_ttl()) {
    // Even though the ttl can be encoded in a single byte, Linux returns it as an `int` when
    // it is received as a control message.
    // https://github.com/torvalds/linux/blob/7e57714cd0a/net/ipv4/ip_sockglue.c#L67
    const int ttl = static_cast<int>(control_data.ttl());
    total += StoreControlMessage(IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
  }

  return total;
}

socklen_t FidlControlDataProcessor::Store(fsocket::wire::Ipv6RecvControlData const& control_data,
                                          const RequestedCmsgSet& requested) {
  socklen_t total = 0;

  if (requested.ipv6_tclass() && control_data.has_tclass()) {
    // Even though the traffic class can be encoded in a single byte, Linux returns it as an
    // `int` when it is received as a control message.
    // https://github.com/torvalds/linux/blob/7e57714cd0a/include/net/ipv6.h#L968
    const int tclass = static_cast<int>(control_data.tclass());
    total += StoreControlMessage(IPPROTO_IPV6, IPV6_TCLASS, &tclass, sizeof(tclass));
  }

  if (requested.ipv6_hoplimit() && control_data.has_hoplimit()) {
    // Even though the hop limit can be encoded in a single byte, Linux returns it as an `int`
    // when it is received as a control message.
    // https://github.com/torvalds/linux/blob/7e57714cd0a/net/ipv6/datagram.c#L622
    const int hoplimit = static_cast<int>(control_data.hoplimit());
    total += StoreControlMessage(IPPROTO_IPV6, IPV6_HOPLIMIT, &hoplimit, sizeof(hoplimit));
  }

  if (requested.ipv6_pktinfo() && control_data.has_pktinfo()) {
    const fsocket::wire::Ipv6PktInfoRecvControlData& fidl_pktinfo = control_data.pktinfo();
    in6_pktinfo pktinfo = {
        .ipi6_ifindex = static_cast<unsigned int>(fidl_pktinfo.iface),
    };
    static_assert(
        sizeof(pktinfo.ipi6_addr) == decltype(fidl_pktinfo.header_destination_addr.addr)::size(),
        "mismatch between size of FIDL and in6_pktinfo IPv6 addresses");
    memcpy(&pktinfo.ipi6_addr, fidl_pktinfo.header_destination_addr.addr.data(),
           sizeof(pktinfo.ipi6_addr));
    total += StoreControlMessage(IPPROTO_IPV6, IPV6_PKTINFO, &pktinfo, sizeof(pktinfo));
  }
  return total;
}

socklen_t FidlControlDataProcessor::StoreControlMessage(int level, int type, const void* data,
                                                        socklen_t len) {
  socklen_t cmsg_len = CMSG_LEN(len);
  size_t bytes_left = buffer_.size();
  if (bytes_left < cmsg_len) {
    // Not enough space to store the entire control message.
    // TODO(https://fxbug.dev/86146): Add support for truncated control messages (MSG_CTRUNC).
    return 0;
  }

  // The user-provided pointer is not guaranteed to be aligned. So instead of casting it into
  // a struct cmsghdr and writing to it directly, stack-allocate one and then copy it.
  cmsghdr cmsg = {
      .cmsg_len = cmsg_len,
      .cmsg_level = level,
      .cmsg_type = type,
  };
  unsigned char* buf = buffer_.data();
  ZX_ASSERT_MSG(CMSG_DATA(buf) + len <= buf + bytes_left,
                "buffer would overflow, %p + %x > %p + %zx", CMSG_DATA(buf), len, buf, bytes_left);
  memcpy(buf, &cmsg, sizeof(cmsg));
  memcpy(CMSG_DATA(buf), data, len);
  size_t bytes_consumed = std::min(CMSG_SPACE(len), bytes_left);
  buffer_ = buffer_.subspan(bytes_consumed);

  return static_cast<socklen_t>(bytes_consumed);
}
