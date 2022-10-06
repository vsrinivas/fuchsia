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

int16_t ParseSocketLevelControlMessage(
    fidl::WireTableBuilder<fsocket::wire::SocketSendControlData>& fidl_socket, int type,
    const void* data, socklen_t len) {
  // TODO(https://fxbug.dev/88984): Validate unsupported SOL_SOCKET control messages.
  return 0;
}

int16_t ParseIpLevelControlMessage(
    fidl::WireTableBuilder<fsocket::wire::IpSendControlData>& fidl_ip, int type, const void* data,
    socklen_t len) {
  switch (type) {
    case IP_TTL: {
      int ttl;
      if (len != CMSG_LEN(sizeof(ttl))) {
        return EINVAL;
      }
      memcpy(&ttl, data, sizeof(ttl));
      if (ttl < 0 || ttl > std::numeric_limits<uint8_t>::max()) {
        return EINVAL;
      }
      // N.B. This extra validation is performed here in the client since the payload
      // might be processed by the Netstack asynchronously.
      //
      // See: https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0109_socket_datagram_socket
      if (ttl == 0) {
        return EINVAL;
      }
      fidl_ip.ttl(static_cast<uint8_t>(ttl));
      return 0;
    }
    default:
      // TODO(https://fxbug.dev/88984): Validate unsupported SOL_IP control messages.
      return 0;
  }
}

int16_t ParseIpv6LevelControlMessage(
    fidl::WireTableBuilder<fsocket::wire::Ipv6SendControlData>& fidl_ipv6, int type,
    const void* data, socklen_t data_len) {
  switch (type) {
    case IPV6_HOPLIMIT: {
      int hoplimit;
      if (data_len != CMSG_LEN(sizeof(hoplimit))) {
        return EINVAL;
      }
      memcpy(&hoplimit, data, sizeof(hoplimit));
      if (hoplimit < -1 || hoplimit > std::numeric_limits<uint8_t>::max()) {
        return EINVAL;
      }
      // Ignore hoplimit if it's -1 as it it is interpreted as if the cmsg was not present.
      //
      // https://github.com/torvalds/linux/blob/eaa54b1458c/net/ipv6/udp.c#L1531
      if (hoplimit != -1) {
        fidl_ipv6.hoplimit(static_cast<uint8_t>(hoplimit));
      }
      return 0;
    }
    case IPV6_PKTINFO: {
      in6_pktinfo pktinfo;
      if (data_len != CMSG_LEN(sizeof(pktinfo))) {
        return EINVAL;
      }
      memcpy(&pktinfo, data, sizeof(pktinfo));
      fsocket::wire::Ipv6PktInfoSendControlData fidl_pktinfo{
          .iface = static_cast<uint64_t>(pktinfo.ipi6_ifindex),
      };
      static_assert(sizeof(pktinfo.ipi6_addr) == sizeof(fidl_pktinfo.local_addr.addr),
                    "mismatch between size of FIDL and in6_pktinfo IPv6 addresses");
      memcpy(fidl_pktinfo.local_addr.addr.data(), &pktinfo.ipi6_addr, sizeof(pktinfo.ipi6_addr));
      fidl_ipv6.pktinfo(fidl_pktinfo);
      return 0;
    }
    default:
      // TODO(https://fxbug.dev/88984): Validate unsupported SOL_IPV6 control messages.
      return 0;
  }
}

template <typename F>
int16_t ParseMultipleControlMessages(fidl::AnyArena& allocator, const struct msghdr& msg,
                                     F parse_control_message) {
  if (msg.msg_control == nullptr && msg.msg_controllen != 0) {
    return static_cast<int16_t>(EFAULT);
  }

  socklen_t total_cmsg_len = 0;
  for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
    const cmsghdr& cmsg_ref = *cmsg;
    total_cmsg_len += cmsg_ref.cmsg_len;

    // Validate the header length.
    // https://github.com/torvalds/linux/blob/42eb8fdac2f/include/linux/socket.h#L119-L122
    if (msg.msg_controllen < total_cmsg_len || cmsg_ref.cmsg_len < sizeof(cmsghdr)) {
      return static_cast<int16_t>(EINVAL);
    }

    int16_t err = parse_control_message(allocator, cmsg_ref);
    if (err != 0) {
      return err;
    }
  }
  return 0;
}

fit::result<int16_t, fsocket::wire::NetworkSocketSendControlData> ParseNetworkSocketSendControlData(
    fidl::AnyArena& allocator, const struct msghdr& msg) {
  fidl::WireTableBuilder fidl_socket = fsocket::wire::SocketSendControlData::Builder(allocator);
  fidl::WireTableBuilder fidl_ip = fsocket::wire::IpSendControlData::Builder(allocator);
  fidl::WireTableBuilder fidl_ipv6 = fsocket::wire::Ipv6SendControlData::Builder(allocator);
  int16_t err = ParseMultipleControlMessages(
      allocator, msg,
      [&fidl_socket, &fidl_ip, &fidl_ipv6](fidl::AnyArena& allocator,
                                           const cmsghdr& cmsg) -> int16_t {
        switch (cmsg.cmsg_level) {
          case SOL_SOCKET:
            return ParseSocketLevelControlMessage(fidl_socket, cmsg.cmsg_type, CMSG_DATA(&cmsg),
                                                  cmsg.cmsg_len);
          case SOL_IP:
            return ParseIpLevelControlMessage(fidl_ip, cmsg.cmsg_type, CMSG_DATA(&cmsg),
                                              cmsg.cmsg_len);
          case SOL_IPV6:
            return ParseIpv6LevelControlMessage(fidl_ipv6, cmsg.cmsg_type, CMSG_DATA(&cmsg),
                                                cmsg.cmsg_len);
          default:
            return 0;
        }
      });

  if (err != 0) {
    return fit::error(err);
  }

  return fit::success(fsocket::wire::NetworkSocketSendControlData::Builder(allocator)
                          .socket(fidl_socket.Build())
                          .ip(fidl_ip.Build())
                          .ipv6(fidl_ipv6.Build())
                          .Build());
}

template <>
fit::result<int16_t, fuchsia_posix_socket::wire::DatagramSocketSendControlData>
ParseControlMessages<fuchsia_posix_socket::wire::DatagramSocketSendControlData>(
    fidl::AnyArena& allocator, const struct msghdr& msg) {
  fit::result fidl_net = ParseNetworkSocketSendControlData(allocator, msg);
  if (fidl_net.is_error()) {
    return fidl_net.take_error();
  }

  return fit::success(fuchsia_posix_socket::wire::DatagramSocketSendControlData::Builder(allocator)
                          .network(fidl_net.value())
                          .Build());
}

template <>
fit::result<int16_t, fuchsia_posix_socket::wire::NetworkSocketSendControlData>
ParseControlMessages<fuchsia_posix_socket::wire::NetworkSocketSendControlData>(
    fidl::AnyArena& allocator, const struct msghdr& msg) {
  return ParseNetworkSocketSendControlData(allocator, msg);
}

fit::result<int16_t, fsocket::wire::SocketSendControlData> ParseSocketSendControlData(
    fidl::AnyArena& allocator, const struct msghdr& msg) {
  fidl::WireTableBuilder fidl_socket = fsocket::wire::SocketSendControlData::Builder(allocator);
  int16_t err = ParseMultipleControlMessages(
      allocator, msg, [&fidl_socket](fidl::AnyArena& allocator, const cmsghdr& cmsg) -> int16_t {
        switch (cmsg.cmsg_level) {
          case SOL_SOCKET:
            return ParseSocketLevelControlMessage(fidl_socket, cmsg.cmsg_type, CMSG_DATA(&cmsg),
                                                  cmsg.cmsg_len);
          default:
            return 0;
        }
      });

  if (err != 0) {
    return fit::error(err);
  }

  return fit::success(fidl_socket.Build());
}

template <>
fit::result<int16_t, fuchsia_posix_socket::wire::SocketSendControlData>
ParseControlMessages<fuchsia_posix_socket::wire::SocketSendControlData>(fidl::AnyArena& allocator,
                                                                        const struct msghdr& msg) {
  return ParseSocketSendControlData(allocator, msg);
}

template <>
fit::result<int16_t, fuchsia_posix_socket_packet::wire::SendControlData>
ParseControlMessages<fuchsia_posix_socket_packet::wire::SendControlData>(fidl::AnyArena& allocator,
                                                                         const struct msghdr& msg) {
  fit::result fidl_socket = ParseSocketSendControlData(allocator, msg);
  if (fidl_socket.is_error()) {
    return fidl_socket.take_error();
  }

  return fit::success(fuchsia_posix_socket_packet::wire::SendControlData::Builder(allocator)
                          .socket(fidl_socket.value())
                          .Build());
}
