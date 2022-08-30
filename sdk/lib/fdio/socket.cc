// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdk/lib/fdio/socket.h"

#include <lib/fitx/result.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/socket.h>
#include <lib/zxio/cpp/create_with_type.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/cpp/socket_address.h>
#include <lib/zxio/cpp/vector.h>
#include <lib/zxio/null.h>
#include <net/if.h>
#include <netinet/icmp6.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <poll.h>
#include <sys/ioctl.h>

#include <algorithm>
#include <bitset>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <netpacket/packet.h>

#include "fdio_unistd.h"
#include "sdk/lib/fdio/get_client.h"
#include "src/connectivity/network/netstack/udp_serde/udp_serde.h"
#include "zxio.h"

namespace fsocket = fuchsia_posix_socket;
namespace frawsocket = fuchsia_posix_socket_raw;
namespace fpacketsocket = fuchsia_posix_socket_packet;
namespace fnet = fuchsia_net;

/* Socket class hierarchy
 *
 *  Wrapper structs for supported FIDL protocols that encapsulate associated
 *  types and specialized per-protocol logic.
 *
 *   +-------------------------+  +---------------------+  +-------------------------+
 *   |   struct StreamSocket   |  |  struct RawSocket   |  | struct DatagramSocket   |
 *   |  fsocket::StreamSocket  |  |  frawsocket:Socket  |  | fsocket::DatagramSocket |
 *   +-------------------------+  +---------------------+  +-------------------------+
 *   +-------------------------+  +-------------------------------------+
 *   |   struct PacketSocket   |  |  struct SynchronousDatagramSocket   |
 *   |  fpacketsocket::Socket  |  |  fsocket:SynchronousDatagramSocket  |
 *   +-------------------------+  +-------------------------------------+
 *
 *  Utility classes constructed on-the-fly for common socket operations, enabled
 *  for relevant FIDL wrappers:
 *
 *      +-------------------------+          +-------------------------+
 *      |   class BaseSocket      |          |   class NetworkSocket   |
 *      |                         |          |                         |
 *      |        Enabled:         |          |        Enabled:         |
 *      |        RawSocket        |          |        RawSocket        |
 *      |     SyncDgramSocket     |          |     SyncDgramSocket     |
 *      |       StreamSocket      + -------> |       StreamSocket      |
 *      |       PacketSocket      |          |      DatagramSocket     |
 *      |      DatagramSocket     |          |                         |
 *      |                         |          |       Implements:       |
 *      |       Implements:       |          |  Operations for network |
 *      |     Operations for      |          |        sockets          |
 *      |     all socket types    |          |                         |
 *      +-------------------------+          +-------------------------+
 *
 *  Stateful class hierarchy for wrapping zircon primitives, enabled for
 *  relevant FIDL wrappers:
 *
 *                    +---------------------------+
 *                    | network_socket_with_event | +-----------------+ +-----------------+
 *  +---------------+ |                           | |  stream_socket  | | datagram_socket |
 *  | packet_socket | |         Enabled:          | |                 | |                 |
 *  |               | |         RawSocket         | |    Enabled:     | |    Enabled:     |
 *  |   Enabled:    | |      SyncDgramSocket      | |   StreamSocket  | | DatagramSocket  |
 *  |  PacketSocket | |                           | |                 | |                 |
 *  |               | |                           | |    Implements:  | |    Implements:  |
 *  |  Implements:  | |   Implements: Template    | |  Overrides for  | |  Overrides for  |
 *  | Overrides for | |    for instantiating      | |   SOCK_STREAM   | |   SOCK_DGRAM    |
 *  |    packet     | |   network sockets that    | |  sockets using  | |  sockets using  |
 *  |    sockets    | |  use FIDL over channel    | |  a zx::socket   | |  a zx::socket   |
 *  |  (AF_PACKET)  | |  (SOCK_RAW, SOCK_DGRAM)   | |   data plane    | |   data plane    |
 *  +---------------+ +---------------------------+ +-----------------+ +-----------------+
 *          ^                    ^       ^                   ^                   ^
 *          |                    |       |                   |                   |
 *          |                    |       |                   |                   |
 *          +--------+-----------+       |                   +---------+---------+
 *                   |                   |                             |
 *                   |                   |                             |
 *       +-----------+-----------+       |                +------------+-------------+
 *       |   socket_with_event   |       |                |   socket_with_zx_socket  |
 *       |                       |       |                |                          |
 *       |       Enabled:        |       |                |         Enabled:         |
 *       |     PacketSocket      |       |                |       DatagramSocket     |
 *       |       RawSocket       |       |                |        StreamSocket      |
 *       |    SyncDgramSocket    |       |                |                          |
 *       |                       |       |                |   Implements: Overrides  |
 *       | Implements: Overrides |       |                |    for sockets using a   |
 *       |   for sockets using   |       |                |   zx::socket data plane  |
 *       |   FIDL over channel   |       |                |                          |
 *       |    as a data plane    |       |                |                          |
 *       +-----------------------+       |                +--------------------------+
 *                    ^                  |                             ^
 *                    |                  |                             |
 *                    |                  +----------------+------------+
 *                    |                                   |
 *                    |                                   |
 *                    |                       +-----------+-----------+
 *                    |                       |    network_socket     |
 *                    |                       |                       |
 *         +----------+---------+             |       Enabled:        |
 *         |     base_socket    |             |       RawSocket       |
 *         |                    |             |    SyncDgramSocket    |
 *         |    Enabled: All    +------------>|     Streamsocket      |
 *         |                    |             |                       |
 *         |    Implements:     |             | Implements: Overrides |
 *         | Overrides for all  |             |  for network layer    |
 *         |    socket types    |             |       sockets         |
 *         +--------------------+             +-----------------------+
 *                    ^
 *                    |
 *         +----------+-----------+
 *         |         zxio         |
 *         |                      |
 *         |  Implements: POSIX   |
 *         | interface + behavior |
 *         |    for generic fds   |
 *         +----------------------+
 */

namespace std {
template <>
struct hash<SocketAddress> {
  size_t operator()(const SocketAddress& k) const { return k.hash(); }
};
}  // namespace std

namespace {

// A helper structure to keep a packet info and any members' variants
// allocations on the stack.
struct PacketInfo {
  zx_status_t LoadSockAddr(const sockaddr* addr, size_t addr_len) {
    // Address length larger than sockaddr_storage causes an error for API compatibility only.
    if (addr == nullptr || addr_len > sizeof(sockaddr_storage)) {
      return ZX_ERR_INVALID_ARGS;
    }
    switch (addr->sa_family) {
      case AF_PACKET: {
        if (addr_len < sizeof(sockaddr_ll)) {
          return ZX_ERR_INVALID_ARGS;
        }
        const auto& s = *reinterpret_cast<const sockaddr_ll*>(addr);
        protocol_ = ntohs(s.sll_protocol);
        interface_id_ = s.sll_ifindex;
        switch (s.sll_halen) {
          case 0:
            eui48_storage_.reset();
            return ZX_OK;
          case ETH_ALEN: {
            fnet::wire::MacAddress address;
            static_assert(decltype(address.octets)::size() == ETH_ALEN,
                          "eui48 address must have the same size as ETH_ALEN");
            static_assert(sizeof(s.sll_addr) == ETH_ALEN + 2);
            memcpy(address.octets.data(), s.sll_addr, ETH_ALEN);
            eui48_storage_ = address;
            return ZX_OK;
          }
          default:
            return ZX_ERR_NOT_SUPPORTED;
        }
      }
      default:
        return ZX_ERR_INVALID_ARGS;
    }
  }

  template <typename F>
  std::invoke_result_t<F, fidl::ObjectView<fpacketsocket::wire::PacketInfo>> WithFIDL(F fn) {
    auto packet_info = [this]() -> fpacketsocket::wire::PacketInfo {
      return {
          .protocol = protocol_,
          .interface_id = interface_id_,
          .addr =
              [this]() {
                if (eui48_storage_.has_value()) {
                  return fpacketsocket::wire::HardwareAddress::WithEui48(
                      fidl::ObjectView<fnet::wire::MacAddress>::FromExternal(
                          &eui48_storage_.value()));
                }
                return fpacketsocket::wire::HardwareAddress::WithNone({});
              }(),
      };
    }();
    return fn(fidl::ObjectView<fpacketsocket::wire::PacketInfo>::FromExternal(&packet_info));
  }

 private:
  decltype(fpacketsocket::wire::PacketInfo::protocol) protocol_;
  decltype(fpacketsocket::wire::PacketInfo::interface_id) interface_id_;
  std::optional<fnet::wire::MacAddress> eui48_storage_;
};

std::optional<size_t> total_iov_len(const struct msghdr& msg) {
  size_t total = 0;
  for (int i = 0; i < msg.msg_iovlen; ++i) {
    const iovec& iov = msg.msg_iov[i];
    if (iov.iov_base == nullptr && iov.iov_len != 0) {
      return std::nullopt;
    }
    total += iov.iov_len;
  }
  return total;
}

size_t set_trunc_flags_and_return_out_actual(struct msghdr& msg, size_t written, size_t truncated,
                                             int flags) {
  if (truncated != 0) {
    msg.msg_flags |= MSG_TRUNC;
  } else {
    msg.msg_flags &= ~MSG_TRUNC;
  }
  if ((flags & MSG_TRUNC) != 0) {
    written += truncated;
  }
  return written;
}

uint32_t zxio_signals_to_events(zx_signals_t signals) {
  uint32_t events = 0;
  if (signals & ZXIO_SIGNAL_PEER_CLOSED) {
    events |= POLLIN | POLLOUT | POLLERR | POLLHUP | POLLRDHUP;
  }
  if (signals & ZXIO_SIGNAL_WRITE_DISABLED) {
    events |= POLLHUP | POLLOUT;
  }
  if (signals & ZXIO_SIGNAL_READ_DISABLED) {
    events |= POLLRDHUP | POLLIN;
  }
  return events;
}
int16_t ParseSocketLevelControlMessage(fsocket::wire::SocketSendControlData& fidl_socket, int type,
                                       const void* data, socklen_t len) {
  // TODO(https://fxbug.dev/88984): Validate unsupported SOL_SOCKET control messages.
  return 0;
}

int16_t ParseIpLevelControlMessage(fsocket::wire::IpSendControlData& fidl_ip, int type,
                                   const void* data, socklen_t len) {
  switch (type) {
    case IP_TTL: {
      int ttl;
      if (len != sizeof(ttl)) {
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
      fidl_ip.set_ttl(static_cast<uint8_t>(ttl));
      return 0;
    }
    default:
      // TODO(https://fxbug.dev/88984): Validate unsupported SOL_IP control messages.
      return 0;
  }
}

int16_t ParseIpv6LevelControlMessage(fsocket::wire::Ipv6SendControlData& fidl_ipv6,
                                     fidl::AnyArena& allocator, int type, const void* data,
                                     socklen_t data_len) {
  switch (type) {
    case IPV6_HOPLIMIT: {
      int hoplimit;
      if (data_len != sizeof(hoplimit)) {
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
        fidl_ipv6.set_hoplimit(static_cast<uint8_t>(hoplimit));
      }
      return 0;
    }
    case IPV6_PKTINFO: {
      in6_pktinfo pktinfo;
      if (data_len != sizeof(pktinfo)) {
        return EINVAL;
      }
      memcpy(&pktinfo, data, sizeof(pktinfo));
      fsocket::wire::Ipv6PktInfoSendControlData fidl_pktinfo{
          .iface = static_cast<uint64_t>(pktinfo.ipi6_ifindex),
      };
      static_assert(sizeof(pktinfo.ipi6_addr) == sizeof(fidl_pktinfo.local_addr.addr),
                    "mismatch between size of FIDL and in6_pktinfo IPv6 addresses");
      memcpy(fidl_pktinfo.local_addr.addr.data(), &pktinfo.ipi6_addr, sizeof(pktinfo.ipi6_addr));
      fidl_ipv6.set_pktinfo(allocator, fidl_pktinfo);
      return 0;
    }
    default:
      // TODO(https://fxbug.dev/88984): Validate unsupported SOL_IPV6 control messages.
      return 0;
  }
}

int16_t ParseControlMessage(fsocket::wire::SocketSendControlData& fidl_socket,
                            fidl::AnyArena& allocator, int level, int type, const void* data,
                            socklen_t data_len) {
  switch (level) {
    case SOL_SOCKET:
      return ParseSocketLevelControlMessage(fidl_socket, type, data, data_len);
    default:
      return 0;
  }
}

int16_t ParseControlMessage(fsocket::wire::NetworkSocketSendControlData& fidl_net,
                            fidl::AnyArena& allocator, int level, int type, const void* data,
                            socklen_t data_len) {
  switch (level) {
    case SOL_SOCKET:
      if (!fidl_net.has_socket()) {
        fidl_net.set_socket(allocator, fsocket::wire::SocketSendControlData(allocator));
      }
      return ParseSocketLevelControlMessage(fidl_net.socket(), type, data, data_len);
    case SOL_IP:
      if (!fidl_net.has_ip()) {
        fidl_net.set_ip(allocator, fsocket::wire::IpSendControlData(allocator));
      }
      return ParseIpLevelControlMessage(fidl_net.ip(), type, data, data_len);
    case SOL_IPV6:
      if (!fidl_net.has_ipv6()) {
        fidl_net.set_ipv6(allocator, fsocket::wire::Ipv6SendControlData(allocator));
      }
      return ParseIpv6LevelControlMessage(fidl_net.ipv6(), allocator, type, data, data_len);
    default:
      return 0;
  }
}

int16_t ParseControlMessage(fsocket::wire::DatagramSocketSendControlData& fidl_dgram,
                            fidl::AnyArena& allocator, int level, int type, const void* data,
                            socklen_t data_len) {
  if (!fidl_dgram.has_network()) {
    fidl_dgram.set_network(allocator, fsocket::wire::NetworkSocketSendControlData(allocator));
  }
  return ParseControlMessage(fidl_dgram.network(), allocator, level, type, data, data_len);
}

int16_t ParseControlMessage(fpacketsocket::wire::SendControlData& fidl_packet,
                            fidl::AnyArena& allocator, int level, int type, const void* data,
                            socklen_t data_len) {
  if (!fidl_packet.has_socket()) {
    fidl_packet.set_socket(allocator, fsocket::wire::SocketSendControlData(allocator));
  }
  return ParseControlMessage(fidl_packet.socket(), allocator, level, type, data, data_len);
}

template <typename T>
fitx::result<int16_t, T> ParseControlMessages(const void* buf, socklen_t len,
                                              fidl::AnyArena& allocator) {
  if (buf == nullptr && len != 0) {
    return fitx::error(static_cast<int16_t>(EFAULT));
  }

  T fidl_cmsg(allocator);
  cpp20::span posix_cmsg(static_cast<const unsigned char*>(buf), len);
  // Stop parsing once there is not enough bytes left to form a full cmsghdr.
  // https://github.com/torvalds/linux/blob/42eb8fdac2f/net/core/sock.c#L2644
  // https://github.com/torvalds/linux/blob/42eb8fdac2f/include/linux/socket.h#L115-L126
  while (posix_cmsg.size() >= sizeof(cmsghdr)) {
    // Do not access the control buffer directly, as it may be misaligned.
    cmsghdr cmsg;
    memcpy(&cmsg, posix_cmsg.data(), sizeof(cmsg));

    // Validate the header length.
    // https://github.com/torvalds/linux/blob/42eb8fdac2f/include/linux/socket.h#L119-L122
    if (cmsg.cmsg_len < sizeof(cmsg) || cmsg.cmsg_len > posix_cmsg.size()) {
      return fitx::error(static_cast<int16_t>(EINVAL));
    }
    const void* data = CMSG_DATA(posix_cmsg.data());
    const socklen_t data_len = cmsg.cmsg_len - CMSG_ALIGN(sizeof(cmsghdr));
    ZX_ASSERT_MSG(reinterpret_cast<const unsigned char*>(data) + data_len < posix_cmsg.end(),
                  "incoherent data buffer bounds, %p + %x > %p", data, data_len, posix_cmsg.end());
    posix_cmsg = posix_cmsg.subspan(cmsg.cmsg_len);

    int16_t err =
        ParseControlMessage(fidl_cmsg, allocator, cmsg.cmsg_level, cmsg.cmsg_type, data, data_len);
    if (err != 0) {
      return fitx::error(err);
    }
  }

  return fitx::success(fidl_cmsg);
}

using fsocket::wire::CmsgRequests;

struct RequestedCmsgSet {
 public:
  RequestedCmsgSet(
      const fuchsia_posix_socket::wire::DatagramSocketRecvMsgPostflightResponse& response) {
    if (response.has_requests()) {
      requests_ = response.requests();
    }
    if (response.has_timestamp()) {
      so_timestamp_filter_ = response.timestamp();
    } else {
      so_timestamp_filter_ = fsocket::wire::TimestampOption::kDisabled;
    }
  }

  constexpr static RequestedCmsgSet AllRequestedCmsgSet() {
    RequestedCmsgSet cmsg_set;
    cmsg_set.requests_ |= CmsgRequests::kMask;
    return cmsg_set;
  }

  std::optional<fsocket::wire::TimestampOption> so_timestamp() const {
    return so_timestamp_filter_;
  }

  bool ip_tos() const { return static_cast<bool>(requests_ & CmsgRequests::kIpTos); }

  bool ip_ttl() const { return static_cast<bool>(requests_ & CmsgRequests::kIpTtl); }

  bool ipv6_tclass() const { return static_cast<bool>(requests_ & CmsgRequests::kIpv6Tclass); }

  bool ipv6_hoplimit() const { return static_cast<bool>(requests_ & CmsgRequests::kIpv6Hoplimit); }

  bool ipv6_pktinfo() const { return static_cast<bool>(requests_ & CmsgRequests::kIpv6Pktinfo); }

 private:
  RequestedCmsgSet() = default;
  CmsgRequests requests_;
  std::optional<fsocket::wire::TimestampOption> so_timestamp_filter_;
};

class FidlControlDataProcessor {
 public:
  FidlControlDataProcessor(void* buf, socklen_t len)
      : buffer_(cpp20::span{reinterpret_cast<unsigned char*>(buf), len}) {}

  socklen_t Store(fsocket::wire::DatagramSocketRecvControlData const& control_data,
                  const RequestedCmsgSet& requested) {
    socklen_t total = 0;
    if (control_data.has_network()) {
      total += Store(control_data.network(), requested);
    }
    return total;
  }

  socklen_t Store(fsocket::wire::NetworkSocketRecvControlData const& control_data,
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

  socklen_t Store(fpacketsocket::wire::RecvControlData const& control_data,
                  const RequestedCmsgSet& requested) {
    socklen_t total = 0;
    if (control_data.has_socket()) {
      total += Store(control_data.socket(), requested);
    }
    return total;
  }

 private:
  socklen_t Store(fsocket::wire::SocketRecvControlData const& control_data,
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

  socklen_t Store(fsocket::wire::IpRecvControlData const& control_data,
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

  socklen_t Store(fsocket::wire::Ipv6RecvControlData const& control_data,
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

  socklen_t StoreControlMessage(int level, int type, const void* data, socklen_t len) {
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
                  "buffer would overflow, %p + %x > %p + %zx", CMSG_DATA(buf), len, buf,
                  bytes_left);
    memcpy(buf, &cmsg, sizeof(cmsg));
    memcpy(CMSG_DATA(buf), data, len);
    size_t bytes_consumed = std::min(CMSG_SPACE(len), bytes_left);
    buffer_ = buffer_.subspan(bytes_consumed);

    return static_cast<socklen_t>(bytes_consumed);
  }

  cpp20::span<unsigned char> buffer_;
};  // namespace

fsocket::wire::RecvMsgFlags to_recvmsg_flags(int flags) {
  fsocket::wire::RecvMsgFlags r;
  if (flags & MSG_PEEK) {
    r |= fsocket::wire::RecvMsgFlags::kPeek;
  }
  return r;
}

fsocket::wire::SendMsgFlags to_sendmsg_flags(int flags) { return fsocket::wire::SendMsgFlags(); }

uint8_t fidl_pkttype_to_pkttype(const fpacketsocket::wire::PacketType type) {
  switch (type) {
    case fpacketsocket::wire::PacketType::kHost:
      return PACKET_HOST;
    case fpacketsocket::wire::PacketType::kBroadcast:
      return PACKET_BROADCAST;
    case fpacketsocket::wire::PacketType::kMulticast:
      return PACKET_MULTICAST;
    case fpacketsocket::wire::PacketType::kOtherHost:
      return PACKET_OTHERHOST;
    case fpacketsocket::wire::PacketType::kOutgoing:
      return PACKET_OUTGOING;
  }
}

// https://github.com/torvalds/linux/blob/f2850dd5ee0/include/net/tcp.h#L1012
constexpr socklen_t kTcpCANameMax = 16;
constexpr const char kCcCubic[kTcpCANameMax] = "cubic";
constexpr const char kCcReno[kTcpCANameMax] = "reno";

struct SockOptResult {
  const zx_status_t status;
  const int16_t err;

  bool ok() const { return status == ZX_OK && err == 0; }

  static inline SockOptResult Ok() { return SockOptResult{ZX_OK, 0}; }

  static inline SockOptResult Errno(int16_t err) { return SockOptResult{ZX_OK, err}; }

  static inline SockOptResult Zx(zx_status_t status) { return SockOptResult{status, 0}; }

  template <typename T>
  static inline SockOptResult FromFidlResponse(const T& response) {
    if (response.status() != ZX_OK) {
      return SockOptResult::Zx(response.status());
    }
    const auto& result = response.value();
    if (result.is_error()) {
      return SockOptResult::Errno(static_cast<int16_t>(result.error_value()));
    }
    return SockOptResult::Ok();
  }
};

class GetSockOptProcessor {
 public:
  GetSockOptProcessor(void* optval, socklen_t* optlen) : optval_(optval), optlen_(optlen) {}

  template <typename T, typename F>
  SockOptResult Process(T&& response, F getter) {
    if (response.status() != ZX_OK) {
      return SockOptResult::Zx(response.status());
    }
    const auto& result = response.value();
    if (result.is_error()) {
      return SockOptResult::Errno(static_cast<int16_t>(result.error_value()));
    }
    return StoreOption(getter(*result.value()));
  }

  template <typename T>
  SockOptResult StoreOption(const T& value) {
    static_assert(sizeof(T) != sizeof(T), "function must be specialized");
  }

 private:
  SockOptResult StoreRaw(const void* data, socklen_t data_len) {
    if (data_len > *optlen_) {
      return SockOptResult::Errno(EINVAL);
    }
    memcpy(optval_, data, data_len);
    *optlen_ = data_len;
    return SockOptResult::Ok();
  }

  void* const optval_;
  socklen_t* const optlen_;
};

template <>
SockOptResult GetSockOptProcessor::StoreOption(const int32_t& value) {
  return StoreRaw(&value, sizeof(int32_t));
}

template <>
SockOptResult GetSockOptProcessor::StoreOption(const uint32_t& value) {
  return StoreRaw(&value, sizeof(uint32_t));
}

template <>
SockOptResult GetSockOptProcessor::StoreOption(const uint8_t& value) {
  return StoreRaw(&value, sizeof(uint8_t));
}

template <>
SockOptResult GetSockOptProcessor::StoreOption(const fsocket::wire::Domain& value) {
  int32_t domain;
  switch (value) {
    case fsocket::wire::Domain::kIpv4:
      domain = AF_INET;
      break;
    case fsocket::wire::Domain::kIpv6:
      domain = AF_INET6;
      break;
  }
  return StoreOption(domain);
}

template <>
SockOptResult GetSockOptProcessor::StoreOption(const bool& value) {
  return StoreOption(static_cast<uint32_t>(value));
}

template <>
SockOptResult GetSockOptProcessor::StoreOption(const struct linger& value) {
  return StoreRaw(&value, sizeof(struct linger));
}

template <>
SockOptResult GetSockOptProcessor::StoreOption(const fidl::StringView& value) {
  if (value.empty()) {
    *optlen_ = 0;
  } else if (*optlen_ > value.size()) {
    char* p = std::copy(value.begin(), value.end(), static_cast<char*>(optval_));
    *p = 0;
    *optlen_ = static_cast<socklen_t>(value.size()) + 1;
  } else {
    return SockOptResult::Errno(EINVAL);
  }
  return SockOptResult::Ok();
}

// Helper type to provide GetSockOptProcessor with a truncating string view conversion.
struct TruncatingStringView {
  explicit TruncatingStringView(fidl::StringView string) : string(string) {}

  fidl::StringView string;
};

template <>
SockOptResult GetSockOptProcessor::StoreOption(const TruncatingStringView& value) {
  *optlen_ = std::min(*optlen_, static_cast<socklen_t>(value.string.size()));
  char* p = std::copy_n(value.string.begin(), *optlen_ - 1, static_cast<char*>(optval_));
  *p = 0;
  return SockOptResult::Ok();
}

template <>
SockOptResult GetSockOptProcessor::StoreOption(const fsocket::wire::OptionalUint8& value) {
  switch (value.Which()) {
    case fsocket::wire::OptionalUint8::Tag::kValue:
      return StoreOption(static_cast<int32_t>(value.value()));
    case fsocket::wire::OptionalUint8::Tag::kUnset:
      return StoreOption(-1);
  }
}

template <>
SockOptResult GetSockOptProcessor::StoreOption(const fsocket::wire::OptionalUint32& value) {
  switch (value.Which()) {
    case fsocket::wire::OptionalUint32::Tag::kValue:
      ZX_ASSERT(value.value() < std::numeric_limits<int32_t>::max());
      return StoreOption(static_cast<int32_t>(value.value()));
    case fsocket::wire::OptionalUint32::Tag::kUnset:
      return StoreOption(-1);
  }
}

template <>
SockOptResult GetSockOptProcessor::StoreOption(const fnet::wire::Ipv4Address& value) {
  static_assert(sizeof(struct in_addr) == sizeof(value.addr));
  return StoreRaw(value.addr.data(), sizeof(value.addr));
}

template <>
SockOptResult GetSockOptProcessor::StoreOption(const frawsocket::wire::Icmpv6Filter& value) {
  static_assert(sizeof(icmp6_filter) == sizeof(value.blocked_types));
  *optlen_ = std::min(static_cast<socklen_t>(sizeof(icmp6_filter)), *optlen_);
  memcpy(optval_, value.blocked_types.data(), *optlen_);
  return SockOptResult::Ok();
}

template <>
SockOptResult GetSockOptProcessor::StoreOption(const fsocket::wire::TcpInfo& value) {
  tcp_info info;
  // Explicitly initialize unsupported fields to a garbage value. It would probably be quieter
  // to zero-initialize, but that can mask bugs in the interpretation of fields for which zero
  // is a valid value.
  //
  // Note that "unsupported" includes fields not defined in FIDL *and* fields not populated by
  // the server.
  memset(&info, 0xff, sizeof(info));

  if (value.has_state()) {
    info.tcpi_state = [](fsocket::wire::TcpState state) -> uint8_t {
      switch (state) {
        case fsocket::wire::TcpState::kEstablished:
          return TCP_ESTABLISHED;
        case fsocket::wire::TcpState::kSynSent:
          return TCP_SYN_SENT;
        case fsocket::wire::TcpState::kSynRecv:
          return TCP_SYN_RECV;
        case fsocket::wire::TcpState::kFinWait1:
          return TCP_FIN_WAIT1;
        case fsocket::wire::TcpState::kFinWait2:
          return TCP_FIN_WAIT2;
        case fsocket::wire::TcpState::kTimeWait:
          return TCP_TIME_WAIT;
        case fsocket::wire::TcpState::kClose:
          return TCP_CLOSE;
        case fsocket::wire::TcpState::kCloseWait:
          return TCP_CLOSE_WAIT;
        case fsocket::wire::TcpState::kLastAck:
          return TCP_LAST_ACK;
        case fsocket::wire::TcpState::kListen:
          return TCP_LISTEN;
        case fsocket::wire::TcpState::kClosing:
          return TCP_CLOSING;
      }
    }(value.state());
  }
  if (value.has_ca_state()) {
    info.tcpi_ca_state = [](fsocket::wire::TcpCongestionControlState ca_state) -> uint8_t {
      switch (ca_state) {
        case fsocket::wire::TcpCongestionControlState::kOpen:
          return TCP_CA_Open;
        case fsocket::wire::TcpCongestionControlState::kDisorder:
          return TCP_CA_Disorder;
        case fsocket::wire::TcpCongestionControlState::kCongestionWindowReduced:
          return TCP_CA_CWR;
        case fsocket::wire::TcpCongestionControlState::kRecovery:
          return TCP_CA_Recovery;
        case fsocket::wire::TcpCongestionControlState::kLoss:
          return TCP_CA_Loss;
      }
    }(value.ca_state());
  }
  if (value.has_rto_usec()) {
    info.tcpi_rto = value.rto_usec();
  }
  if (value.has_rtt_usec()) {
    info.tcpi_rtt = value.rtt_usec();
  }
  if (value.has_rtt_var_usec()) {
    info.tcpi_rttvar = value.rtt_var_usec();
  }
  if (value.has_snd_ssthresh()) {
    info.tcpi_snd_ssthresh = value.snd_ssthresh();
  }
  if (value.has_snd_cwnd()) {
    info.tcpi_snd_cwnd = value.snd_cwnd();
  }
  if (value.has_reorder_seen()) {
    info.tcpi_reord_seen = value.reorder_seen();
  }

  static_assert(sizeof(info) <= std::numeric_limits<socklen_t>::max());
  return StoreRaw(&info, std::min(*optlen_, static_cast<socklen_t>(sizeof(info))));
}

// Used for various options that allow the caller to supply larger buffers than needed.
struct PartialCopy {
  int32_t value;
  // Appears to be true for IP_*, SO_* and false for IPV6_*.
  bool allow_char;
};

template <>
SockOptResult GetSockOptProcessor::StoreOption(const PartialCopy& value) {
  socklen_t want_size =
      *optlen_ < sizeof(int32_t) && value.allow_char ? sizeof(uint8_t) : sizeof(value.value);
  *optlen_ = std::min(want_size, *optlen_);
  memcpy(optval_, &value.value, *optlen_);
  return SockOptResult::Ok();
}

class SetSockOptProcessor {
 public:
  SetSockOptProcessor(const void* optval, socklen_t optlen) : optval_(optval), optlen_(optlen) {}

  template <typename T>
  int16_t Get(T& out) {
    if (optlen_ < sizeof(T)) {
      return EINVAL;
    }
    memcpy(&out, optval_, sizeof(T));
    return 0;
  }

  template <typename T, typename F>
  SockOptResult Process(F f) {
    T v;
    int16_t result = Get(v);
    if (result) {
      return SockOptResult::Errno(result);
    }
    return SockOptResult::FromFidlResponse(f(std::move(v)));
  }

 private:
  const void* const optval_;
  socklen_t const optlen_;
};

template <>
int16_t SetSockOptProcessor::Get(fidl::StringView& out) {
  const char* optval = static_cast<const char*>(optval_);
  out = fidl::StringView::FromExternal(optval, strnlen(optval, optlen_));
  return 0;
}

template <>
int16_t SetSockOptProcessor::Get(bool& out) {
  int32_t i;
  int16_t r = Get(i);
  out = i != 0;
  return r;
}

template <>
int16_t SetSockOptProcessor::Get(uint32_t& out) {
  int32_t& alt = *reinterpret_cast<int32_t*>(&out);
  if (int16_t r = Get(alt); r) {
    return r;
  }
  if (alt < 0) {
    return EINVAL;
  }
  return 0;
}

template <>
int16_t SetSockOptProcessor::Get(fsocket::wire::OptionalUint8& out) {
  int32_t i;
  if (int16_t r = Get(i); r) {
    return r;
  }
  if (i < -1 || i > std::numeric_limits<uint8_t>::max()) {
    return EINVAL;
  }
  if (i == -1) {
    out = fsocket::wire::OptionalUint8::WithUnset({});
  } else {
    out = fsocket::wire::OptionalUint8::WithValue(static_cast<uint8_t>(i));
  }
  return 0;
}

// Like OptionalUint8, but permits truncation to a single byte.
struct OptionalUint8CharAllowed {
  fsocket::wire::OptionalUint8 inner;
};

template <>
int16_t SetSockOptProcessor::Get(OptionalUint8CharAllowed& out) {
  if (optlen_ == sizeof(uint8_t)) {
    out.inner = fsocket::wire::OptionalUint8::WithValue(*static_cast<const uint8_t*>(optval_));
    return 0;
  }
  return Get(out.inner);
}

template <>
int16_t SetSockOptProcessor::Get(fsocket::wire::IpMulticastMembership& out) {
  union {
    struct ip_mreqn reqn;
    struct ip_mreq req;
  } r;
  struct in_addr* local;
  struct in_addr* mcast;
  if (optlen_ < sizeof(struct ip_mreqn)) {
    if (Get(r.req) != 0) {
      return EINVAL;
    }
    out.iface = 0;
    local = &r.req.imr_interface;
    mcast = &r.req.imr_multiaddr;
  } else {
    if (Get(r.reqn) != 0) {
      return EINVAL;
    }
    out.iface = r.reqn.imr_ifindex;
    local = &r.reqn.imr_address;
    mcast = &r.reqn.imr_multiaddr;
  }
  static_assert(sizeof(out.local_addr.addr) == sizeof(*local));
  memcpy(out.local_addr.addr.data(), local, sizeof(*local));
  static_assert(sizeof(out.mcast_addr.addr) == sizeof(*mcast));
  memcpy(out.mcast_addr.addr.data(), mcast, sizeof(*mcast));
  return 0;
}

template <>
int16_t SetSockOptProcessor::Get(fsocket::wire::Ipv6MulticastMembership& out) {
  struct ipv6_mreq req;
  if (Get(req) != 0) {
    return EINVAL;
  }
  out.iface = req.ipv6mr_interface;
  static_assert(std::size(req.ipv6mr_multiaddr.s6_addr) == decltype(out.mcast_addr.addr)::size());
  std::copy(std::begin(req.ipv6mr_multiaddr.s6_addr), std::end(req.ipv6mr_multiaddr.s6_addr),
            out.mcast_addr.addr.begin());
  return 0;
}

template <>
int16_t SetSockOptProcessor::Get(frawsocket::wire::Icmpv6Filter& out) {
  struct icmp6_filter filter;
  if (Get(filter) != 0) {
    return EINVAL;
  }

  static_assert(sizeof(filter) == sizeof(out.blocked_types));
  memcpy(out.blocked_types.data(), &filter, sizeof(filter));
  return 0;
}

template <>
int16_t SetSockOptProcessor::Get(fsocket::wire::TcpCongestionControl& out) {
  if (strncmp(static_cast<const char*>(optval_), kCcCubic, optlen_) == 0) {
    out = fsocket::wire::TcpCongestionControl::kCubic;
    return 0;
  }
  if (strncmp(static_cast<const char*>(optval_), kCcReno, optlen_) == 0) {
    out = fsocket::wire::TcpCongestionControl::kReno;
    return 0;
  }
  return ENOENT;
}

struct IntOrChar {
  int32_t value;
};

template <>
int16_t SetSockOptProcessor::Get(IntOrChar& out) {
  if (Get(out.value) == 0) {
    return 0;
  }
  if (optlen_ == 0) {
    return EINVAL;
  }
  out.value = *static_cast<const uint8_t*>(optval_);
  return 0;
}

template <typename T,
          typename = std::enable_if_t<
              std::is_same_v<T, fidl::WireSyncClient<fsocket::SynchronousDatagramSocket>> ||
              std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>> ||
              std::is_same_v<T, fidl::WireSyncClient<frawsocket::Socket>> ||
              std::is_same_v<T, fidl::WireSyncClient<fpacketsocket::Socket>> ||
              std::is_same_v<T, fidl::WireSyncClient<fsocket::DatagramSocket>>>>
struct BaseSocket {
  static_assert(std::is_same_v<T, fidl::WireSyncClient<fsocket::SynchronousDatagramSocket>> ||
                std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>> ||
                std::is_same_v<T, fidl::WireSyncClient<frawsocket::Socket>> ||
                std::is_same_v<T, fidl::WireSyncClient<fpacketsocket::Socket>> ||
                std::is_same_v<T, fidl::WireSyncClient<fsocket::DatagramSocket>>);

 public:
  explicit BaseSocket(T& client) : client_(client) {}

  T& client() { return client_; }

  SockOptResult get_solsocket_sockopt_fidl(int optname, void* optval, socklen_t* optlen) {
    GetSockOptProcessor proc(optval, optlen);
    switch (optname) {
      case SO_TYPE:
        if constexpr (std::is_same_v<T, fidl::WireSyncClient<fsocket::DatagramSocket>> ||
                      std::is_same_v<T, fidl::WireSyncClient<fsocket::SynchronousDatagramSocket>>) {
          return proc.StoreOption<int32_t>(SOCK_DGRAM);
        }
        if constexpr (std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>>) {
          return proc.StoreOption<int32_t>(SOCK_STREAM);
        }
        if constexpr (std::is_same_v<T, fidl::WireSyncClient<frawsocket::Socket>>) {
          return proc.StoreOption<int32_t>(SOCK_RAW);
        }
        if constexpr (std::is_same_v<T, fidl::WireSyncClient<fpacketsocket::Socket>>) {
          return proc.Process(client()->GetInfo(), [](const auto& response) {
            switch (response.kind) {
              case fpacketsocket::wire::Kind::kNetwork:
                return SOCK_DGRAM;
              case fpacketsocket::wire::Kind::kLink:
                return SOCK_RAW;
            }
          });
        }
      case SO_DOMAIN:
        if constexpr (std::is_same_v<T, fidl::WireSyncClient<fpacketsocket::Socket>>) {
          return proc.StoreOption<int32_t>(AF_PACKET);
        } else {
          return proc.Process(client()->GetInfo(),
                              [](const auto& response) { return response.domain; });
        }
      case SO_TIMESTAMP:
        return proc.Process(client()->GetTimestamp(), [](const auto& response) {
          return PartialCopy{
              .value = response.value == fsocket::wire::TimestampOption::kMicrosecond,
              .allow_char = false,
          };
        });
      case SO_TIMESTAMPNS:
        return proc.Process(client()->GetTimestamp(), [](const auto& response) {
          return PartialCopy{
              .value = response.value == fsocket::wire::TimestampOption::kNanosecond,
              .allow_char = false,
          };
        });
      case SO_PROTOCOL:
        if constexpr (std::is_same_v<T, fidl::WireSyncClient<fsocket::DatagramSocket>> ||
                      std::is_same_v<T, fidl::WireSyncClient<fsocket::SynchronousDatagramSocket>>) {
          return proc.Process(client()->GetInfo(), [](const auto& response) {
            switch (response.proto) {
              case fsocket::wire::DatagramSocketProtocol::kUdp:
                return IPPROTO_UDP;
              case fsocket::wire::DatagramSocketProtocol::kIcmpEcho:
                switch (response.domain) {
                  case fsocket::wire::Domain::kIpv4:
                    return IPPROTO_ICMP;
                  case fsocket::wire::Domain::kIpv6:
                    return IPPROTO_ICMPV6;
                }
            }
          });
        }
        if constexpr (std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>>) {
          return proc.Process(client()->GetInfo(), [](const auto& response) {
            switch (response.proto) {
              case fsocket::wire::StreamSocketProtocol::kTcp:
                return IPPROTO_TCP;
            }
          });
        }
        if constexpr (std::is_same_v<T, fidl::WireSyncClient<frawsocket::Socket>>) {
          return proc.Process(client()->GetInfo(), [](const auto& response) {
            switch (response.proto.Which()) {
              case frawsocket::wire::ProtocolAssociation::Tag::kUnassociated:
                return IPPROTO_RAW;
              case frawsocket::wire::ProtocolAssociation::Tag::kAssociated:
                return static_cast<int>(response.proto.associated());
            }
          });
        }
        if constexpr (std::is_same_v<T, fidl::WireSyncClient<fpacketsocket::Socket>>) {
          return proc.StoreOption<int32_t>(0);
        }
      case SO_ERROR: {
        auto response = client()->GetError();
        if (response.status() != ZX_OK) {
          return SockOptResult::Zx(response.status());
        }
        int32_t error_code = 0;
        const auto& result = response.value();
        if (result.is_error()) {
          error_code = static_cast<int32_t>(result.error_value());
        }
        return proc.StoreOption(error_code);
      }
      case SO_SNDBUF:
        return proc.Process(client()->GetSendBuffer(), [](const auto& response) {
          return static_cast<uint32_t>(response.value_bytes);
        });
      case SO_RCVBUF:
        return proc.Process(client()->GetReceiveBuffer(), [](const auto& response) {
          return static_cast<uint32_t>(response.value_bytes);
        });
      case SO_REUSEADDR:
        return proc.Process(client()->GetReuseAddress(),
                            [](const auto& response) { return response.value; });
      case SO_REUSEPORT:
        return proc.Process(client()->GetReusePort(),
                            [](const auto& response) { return response.value; });
      case SO_BINDTODEVICE:
        return proc.Process(
            client()->GetBindToDevice(),
            [](auto& response) -> const fidl::StringView& { return response.value; });
      case SO_BROADCAST:
        return proc.Process(client()->GetBroadcast(),
                            [](const auto& response) { return response.value; });
      case SO_KEEPALIVE:
        return proc.Process(client()->GetKeepAlive(),
                            [](const auto& response) { return response.value; });
      case SO_LINGER:
        return proc.Process(client()->GetLinger(), [](const auto& response) {
          struct linger l;
          l.l_onoff = response.linger;
          // NB: l_linger is typed as int but interpreted as unsigned by
          // linux.
          l.l_linger = static_cast<int>(response.length_secs);
          return l;
        });
      case SO_ACCEPTCONN:
        return proc.Process(client()->GetAcceptConn(),
                            [](const auto& response) { return response.value; });
      case SO_OOBINLINE:
        return proc.Process(client()->GetOutOfBandInline(),
                            [](const auto& response) { return response.value; });
      case SO_NO_CHECK:
        return proc.Process(client()->GetNoCheck(), [](const auto& response) {
          return PartialCopy{
              .value = response.value,
              .allow_char = false,
          };
        });
      case SO_SNDTIMEO:
      case SO_RCVTIMEO:
      case SO_PEERCRED:
        return SockOptResult::Errno(EOPNOTSUPP);
      default:
        return SockOptResult::Errno(ENOPROTOOPT);
    }
  }

  SockOptResult set_solsocket_sockopt_fidl(int optname, const void* optval, socklen_t optlen) {
    SetSockOptProcessor proc(optval, optlen);
    switch (optname) {
      case SO_TIMESTAMP:
        return proc.Process<bool>([this](bool value) {
          using fsocket::wire::TimestampOption;
          TimestampOption opt = value ? TimestampOption::kMicrosecond : TimestampOption::kDisabled;
          return client()->SetTimestamp(opt);
        });
      case SO_TIMESTAMPNS:
        return proc.Process<bool>([this](bool value) {
          using fsocket::wire::TimestampOption;
          TimestampOption opt = value ? TimestampOption::kNanosecond : TimestampOption::kDisabled;
          return client()->SetTimestamp(opt);
        });
      case SO_SNDBUF:
        return proc.Process<int32_t>([this](int32_t value) {
          // NB: SNDBUF treated as unsigned, we just cast the value to skip sign check.
          return client()->SetSendBuffer(static_cast<uint64_t>(value));
        });
      case SO_RCVBUF:
        return proc.Process<int32_t>([this](int32_t value) {
          // NB: RCVBUF treated as unsigned, we just cast the value to skip sign check.
          return client()->SetReceiveBuffer(static_cast<uint64_t>(value));
        });
      case SO_REUSEADDR:
        return proc.Process<bool>([this](bool value) { return client()->SetReuseAddress(value); });
      case SO_REUSEPORT:
        return proc.Process<bool>([this](bool value) { return client()->SetReusePort(value); });
      case SO_BINDTODEVICE:
        return proc.Process<fidl::StringView>(
            [this](fidl::StringView value) { return client()->SetBindToDevice(value); });
      case SO_BROADCAST:
        return proc.Process<bool>([this](bool value) { return client()->SetBroadcast(value); });
      case SO_KEEPALIVE:
        return proc.Process<bool>([this](bool value) { return client()->SetKeepAlive(value); });
      case SO_LINGER:
        return proc.Process<struct linger>([this](struct linger value) {
          // NB: l_linger is typed as int but interpreted as unsigned by linux.
          return client()->SetLinger(value.l_onoff != 0, static_cast<uint32_t>(value.l_linger));
        });
      case SO_OOBINLINE:
        return proc.Process<bool>(
            [this](bool value) { return client()->SetOutOfBandInline(value); });
      case SO_NO_CHECK:
        return proc.Process<bool>([this](bool value) { return client()->SetNoCheck(value); });
      case SO_SNDTIMEO:
      case SO_RCVTIMEO:
        return SockOptResult::Errno(ENOTSUP);
      default:
        return SockOptResult::Errno(ENOPROTOOPT);
    }
  }

 private:
  T& client_;
};

}  // namespace

namespace fdio_internal {

template <typename T,
          typename = std::enable_if_t<
              std::is_same_v<T, fidl::WireSyncClient<fsocket::SynchronousDatagramSocket>> ||
              std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>> ||
              std::is_same_v<T, fidl::WireSyncClient<frawsocket::Socket>> ||
              std::is_same_v<T, fidl::WireSyncClient<fsocket::DatagramSocket>>>>
struct BaseNetworkSocket : public BaseSocket<T> {
  static_assert(std::is_same_v<T, fidl::WireSyncClient<fsocket::SynchronousDatagramSocket>> ||
                std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>> ||
                std::is_same_v<T, fidl::WireSyncClient<frawsocket::Socket>> ||
                std::is_same_v<T, fidl::WireSyncClient<fsocket::DatagramSocket>>);

 public:
  using BaseSocket = BaseSocket<T>;
  using BaseSocket::client;

  explicit BaseNetworkSocket(T& client) : BaseSocket(client) {}

  SockOptResult getsockopt_fidl(int level, int optname, void* optval, socklen_t* optlen) {
    GetSockOptProcessor proc(optval, optlen);
    switch (level) {
      case SOL_SOCKET:
        return BaseSocket::get_solsocket_sockopt_fidl(optname, optval, optlen);
      case SOL_IP:
        switch (optname) {
          case IP_TTL:
            return proc.Process(client()->GetIpTtl(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = true,
              };
            });
          case IP_RECVTTL:
            return proc.Process(client()->GetIpReceiveTtl(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = true,
              };
            });
          case IP_MULTICAST_TTL:
            return proc.Process(client()->GetIpMulticastTtl(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = true,
              };
            });
          case IP_MULTICAST_IF:
            return proc.Process(client()->GetIpMulticastInterface(),
                                [](const auto& response) { return response.value; });
          case IP_MULTICAST_LOOP:
            return proc.Process(client()->GetIpMulticastLoopback(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = true,
              };
            });
          case IP_TOS:
            return proc.Process(client()->GetIpTypeOfService(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = true,
              };
            });
          case IP_RECVTOS:
            return proc.Process(client()->GetIpReceiveTypeOfService(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = true,
              };
            });
          case IP_PKTINFO:
            return proc.Process(client()->GetIpPacketInfo(),
                                [](const auto& response) { return response.value; });
          default:
            return SockOptResult::Errno(ENOPROTOOPT);
        }
      case SOL_IPV6:
        switch (optname) {
          case IPV6_V6ONLY:
            return proc.Process(client()->GetIpv6Only(),
                                [](const auto& response) { return response.value; });
          case IPV6_TCLASS:
            return proc.Process(client()->GetIpv6TrafficClass(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = false,
              };
            });
          case IPV6_MULTICAST_IF:
            return proc.Process(client()->GetIpv6MulticastInterface(), [](const auto& response) {
              return static_cast<uint32_t>(response.value);
            });
          case IPV6_UNICAST_HOPS:
            return proc.Process(client()->GetIpv6UnicastHops(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = false,
              };
            });
          case IPV6_MULTICAST_HOPS:
            return proc.Process(client()->GetIpv6MulticastHops(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = false,
              };
            });
          case IPV6_MULTICAST_LOOP:
            return proc.Process(client()->GetIpv6MulticastLoopback(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = false,
              };
            });
          case IPV6_RECVTCLASS:
            return proc.Process(client()->GetIpv6ReceiveTrafficClass(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = false,
              };
            });
          case IPV6_RECVHOPLIMIT:
            return proc.Process(client()->GetIpv6ReceiveHopLimit(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = false,
              };
            });
          case IPV6_RECVPKTINFO:
            return proc.Process(client()->GetIpv6ReceivePacketInfo(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = false,
              };
            });
          default:
            return SockOptResult::Errno(ENOPROTOOPT);
        }
      case SOL_TCP:
        if constexpr (std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>>) {
          switch (optname) {
            case TCP_NODELAY:
              return proc.Process(client()->GetTcpNoDelay(),
                                  [](const auto& response) { return response.value; });
            case TCP_CORK:
              return proc.Process(client()->GetTcpCork(),
                                  [](const auto& response) { return response.value; });
            case TCP_QUICKACK:
              return proc.Process(client()->GetTcpQuickAck(),
                                  [](const auto& response) { return response.value; });
            case TCP_MAXSEG:
              return proc.Process(client()->GetTcpMaxSegment(),
                                  [](const auto& response) { return response.value_bytes; });
            case TCP_KEEPIDLE:
              return proc.Process(client()->GetTcpKeepAliveIdle(),
                                  [](const auto& response) { return response.value_secs; });
            case TCP_KEEPINTVL:
              return proc.Process(client()->GetTcpKeepAliveInterval(),
                                  [](const auto& response) { return response.value_secs; });
            case TCP_KEEPCNT:
              return proc.Process(client()->GetTcpKeepAliveCount(),
                                  [](const auto& response) { return response.value; });
            case TCP_USER_TIMEOUT:
              return proc.Process(client()->GetTcpUserTimeout(),
                                  [](const auto& response) { return response.value_millis; });
            case TCP_CONGESTION:
              return proc.Process(client()->GetTcpCongestion(), [](const auto& response) {
                switch (response.value) {
                  case fsocket::wire::TcpCongestionControl::kCubic:
                    return TruncatingStringView(
                        fidl::StringView::FromExternal(kCcCubic, sizeof(kCcCubic)));
                  case fsocket::wire::TcpCongestionControl::kReno:
                    return TruncatingStringView(
                        fidl::StringView::FromExternal(kCcReno, sizeof(kCcReno)));
                }
              });
            case TCP_DEFER_ACCEPT:
              return proc.Process(client()->GetTcpDeferAccept(),
                                  [](const auto& response) { return response.value_secs; });
            case TCP_INFO:
              return proc.Process(client()->GetTcpInfo(), [](const auto& response) -> const auto& {
                return response.info;
              });
            case TCP_SYNCNT:
              return proc.Process(client()->GetTcpSynCount(),
                                  [](const auto& response) { return response.value; });
            case TCP_WINDOW_CLAMP:
              return proc.Process(client()->GetTcpWindowClamp(),
                                  [](const auto& response) { return response.value; });
            case TCP_LINGER2:
              return proc.Process(client()->GetTcpLinger(),
                                  [](const auto& response) -> const fsocket::wire::OptionalUint32& {
                                    return response.value_secs;
                                  });
            default:
              return SockOptResult::Errno(ENOPROTOOPT);
          }
        } else {
          __FALLTHROUGH;
        }
      default:
        return SockOptResult::Errno(EPROTONOSUPPORT);
    }
  }

  SockOptResult setsockopt_fidl(int level, int optname, const void* optval, socklen_t optlen) {
    SetSockOptProcessor proc(optval, optlen);
    switch (level) {
      case SOL_SOCKET:
        return BaseSocket::set_solsocket_sockopt_fidl(optname, optval, optlen);
      case SOL_IP:
        switch (optname) {
          case IP_MULTICAST_TTL:
            return proc.Process<OptionalUint8CharAllowed>([this](OptionalUint8CharAllowed value) {
              return client()->SetIpMulticastTtl(value.inner);
            });
          case IP_ADD_MEMBERSHIP: {
            return proc.Process<fsocket::wire::IpMulticastMembership>(
                [this](fsocket::wire::IpMulticastMembership value) {
                  return client()->AddIpMembership(value);
                });
          }
          case IP_DROP_MEMBERSHIP:
            return proc.Process<fsocket::wire::IpMulticastMembership>(
                [this](fsocket::wire::IpMulticastMembership value) {
                  return client()->DropIpMembership(value);
                });
          case IP_MULTICAST_IF: {
            if (optlen == sizeof(struct in_addr)) {
              return proc.Process<struct in_addr>([this](struct in_addr value) {
                fnet::wire::Ipv4Address addr;
                static_assert(sizeof(addr.addr) == sizeof(value.s_addr));
                memcpy(addr.addr.data(), &value.s_addr, sizeof(value.s_addr));
                return client()->SetIpMulticastInterface(0, addr);
              });
            }
            return proc.Process<fsocket::wire::IpMulticastMembership>(
                [this](fsocket::wire::IpMulticastMembership value) {
                  return client()->SetIpMulticastInterface(value.iface, value.local_addr);
                });
          }
          case IP_MULTICAST_LOOP:
            return proc.Process<IntOrChar>([this](IntOrChar value) {
              return client()->SetIpMulticastLoopback(value.value != 0);
            });
          case IP_TTL:
            return proc.Process<OptionalUint8CharAllowed>(
                [this](OptionalUint8CharAllowed value) { return client()->SetIpTtl(value.inner); });
          case IP_RECVTTL:
            return proc.Process<IntOrChar>(
                [this](IntOrChar value) { return client()->SetIpReceiveTtl(value.value != 0); });
          case IP_TOS:
            if (optlen == 0) {
              return SockOptResult::Ok();
            }
            return proc.Process<IntOrChar>([this](IntOrChar value) {
              return client()->SetIpTypeOfService(static_cast<uint8_t>(value.value));
            });
          case IP_RECVTOS:
            return proc.Process<IntOrChar>([this](IntOrChar value) {
              return client()->SetIpReceiveTypeOfService(value.value != 0);
            });
          case IP_PKTINFO:
            return proc.Process<IntOrChar>(
                [this](IntOrChar value) { return client()->SetIpPacketInfo(value.value != 0); });
          case MCAST_JOIN_GROUP:
            return SockOptResult::Errno(ENOTSUP);
          default:
            return SockOptResult::Errno(ENOPROTOOPT);
        }
      case SOL_IPV6:
        switch (optname) {
          case IPV6_V6ONLY:
            return proc.Process<bool>([this](bool value) { return client()->SetIpv6Only(value); });
          case IPV6_ADD_MEMBERSHIP:
            return proc.Process<fsocket::wire::Ipv6MulticastMembership>(
                [this](fsocket::wire::Ipv6MulticastMembership value) {
                  return client()->AddIpv6Membership(value);
                });
          case IPV6_DROP_MEMBERSHIP:
            return proc.Process<fsocket::wire::Ipv6MulticastMembership>(
                [this](fsocket::wire::Ipv6MulticastMembership value) {
                  return client()->DropIpv6Membership(value);
                });
          case IPV6_MULTICAST_IF:
            return proc.Process<IntOrChar>([this](IntOrChar value) {
              return client()->SetIpv6MulticastInterface(value.value);
            });
          case IPV6_UNICAST_HOPS:
            return proc.Process<fsocket::wire::OptionalUint8>(
                [this](fsocket::wire::OptionalUint8 value) {
                  return client()->SetIpv6UnicastHops(value);
                });
          case IPV6_MULTICAST_HOPS:
            return proc.Process<fsocket::wire::OptionalUint8>(
                [this](fsocket::wire::OptionalUint8 value) {
                  return client()->SetIpv6MulticastHops(value);
                });
          case IPV6_MULTICAST_LOOP:
            return proc.Process<bool>(
                [this](bool value) { return client()->SetIpv6MulticastLoopback(value); });
          case IPV6_TCLASS:
            return proc.Process<fsocket::wire::OptionalUint8>(
                [this](fsocket::wire::OptionalUint8 value) {
                  return client()->SetIpv6TrafficClass(value);
                });
          case IPV6_RECVTCLASS:
            return proc.Process<bool>(
                [this](bool value) { return client()->SetIpv6ReceiveTrafficClass(value); });
          case IPV6_RECVHOPLIMIT:
            return proc.Process<bool>(
                [this](bool value) { return client()->SetIpv6ReceiveHopLimit(value); });
          case IPV6_RECVPKTINFO:
            return proc.Process<bool>(
                [this](bool value) { return client()->SetIpv6ReceivePacketInfo(value); });
          default:
            return SockOptResult::Errno(ENOPROTOOPT);
        }
      case SOL_TCP:
        if constexpr (std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>>) {
          switch (optname) {
            case TCP_NODELAY:
              return proc.Process<bool>(
                  [this](bool value) { return client()->SetTcpNoDelay(value); });
            case TCP_CORK:
              return proc.Process<bool>([this](bool value) { return client()->SetTcpCork(value); });
            case TCP_QUICKACK:
              return proc.Process<bool>(
                  [this](bool value) { return client()->SetTcpQuickAck(value); });
            case TCP_MAXSEG:
              return proc.Process<uint32_t>(
                  [this](uint32_t value) { return client()->SetTcpMaxSegment(value); });
            case TCP_KEEPIDLE:
              return proc.Process<uint32_t>(
                  [this](uint32_t value) { return client()->SetTcpKeepAliveIdle(value); });
            case TCP_KEEPINTVL:
              return proc.Process<uint32_t>(
                  [this](uint32_t value) { return client()->SetTcpKeepAliveInterval(value); });
            case TCP_KEEPCNT:
              return proc.Process<uint32_t>(
                  [this](uint32_t value) { return client()->SetTcpKeepAliveCount(value); });
            case TCP_USER_TIMEOUT:
              return proc.Process<uint32_t>(
                  [this](uint32_t value) { return client()->SetTcpUserTimeout(value); });
            case TCP_CONGESTION:
              return proc.Process<fsocket::wire::TcpCongestionControl>(
                  [this](fsocket::wire::TcpCongestionControl value) {
                    return client()->SetTcpCongestion(value);
                  });
            case TCP_DEFER_ACCEPT:
              return proc.Process<int32_t>([this](int32_t value) {
                if (value < 0) {
                  value = 0;
                }
                return client()->SetTcpDeferAccept(value);
              });
            case TCP_SYNCNT:
              return proc.Process<uint32_t>(
                  [this](uint32_t value) { return client()->SetTcpSynCount(value); });
            case TCP_WINDOW_CLAMP:
              return proc.Process<uint32_t>(
                  [this](uint32_t value) { return client()->SetTcpWindowClamp(value); });
            case TCP_LINGER2:
              return proc.Process<int32_t>([this](int32_t value) {
                fsocket::wire::OptionalUint32 opt;
                if (value < 0) {
                  opt = fsocket::wire::OptionalUint32::WithUnset({});
                } else {
                  opt = fsocket::wire::OptionalUint32::WithValue(static_cast<uint32_t>(value));
                }
                return client()->SetTcpLinger(opt);
              });
            default:
              return SockOptResult::Errno(ENOPROTOOPT);
          }
        } else {
          __FALLTHROUGH;
        }
      default:
        return SockOptResult::Errno(EPROTONOSUPPORT);
    }
  }

  zx_status_t shutdown(int how, int16_t* out_code) {
    using fsocket::wire::ShutdownMode;
    ShutdownMode mode;
    switch (how) {
      case SHUT_RD:
        mode = ShutdownMode::kRead;
        break;
      case SHUT_WR:
        mode = ShutdownMode::kWrite;
        break;
      case SHUT_RDWR:
        mode = ShutdownMode::kRead | ShutdownMode::kWrite;
        break;
      default:
        return ZX_ERR_INVALID_ARGS;
    }

    auto response = client()->Shutdown(mode);
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
      return ZX_OK;
    }
    *out_code = 0;
    return ZX_OK;
  }
};

}  // namespace fdio_internal

namespace {

// Prevent divergence in flag bitmasks between libc and fuchsia.posix.socket FIDL library.
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::kUp) == IFF_UP);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::kBroadcast) == IFF_BROADCAST);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::kDebug) == IFF_DEBUG);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::kLoopback) == IFF_LOOPBACK);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::kPointtopoint) ==
              IFF_POINTOPOINT);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::kNotrailers) == IFF_NOTRAILERS);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::kRunning) == IFF_RUNNING);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::kNoarp) == IFF_NOARP);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::kPromisc) == IFF_PROMISC);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::kAllmulti) == IFF_ALLMULTI);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::kLeader) == IFF_MASTER);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::kFollower) == IFF_SLAVE);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::kMulticast) == IFF_MULTICAST);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::kPortsel) == IFF_PORTSEL);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::kAutomedia) == IFF_AUTOMEDIA);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::kDynamic) == IFF_DYNAMIC);

}  // namespace

namespace fdio_internal {

struct SynchronousDatagramSocket;
struct RawSocket;
struct PacketSocket;
struct StreamSocket;
struct DatagramSocket;

template <typename T, typename = std::enable_if_t<
                          std::is_same_v<T, SynchronousDatagramSocket> ||
                          std::is_same_v<T, RawSocket> || std::is_same_v<T, PacketSocket> ||
                          std::is_same_v<T, StreamSocket> || std::is_same_v<T, DatagramSocket>>>
struct base_socket : public zxio {
  static constexpr zx_signals_t kSignalError = ZX_USER_SIGNAL_2;

  Errno posix_ioctl(int req, va_list va) final {
    switch (req) {
      case SIOCGIFNAME: {
        auto& provider = get_client<fsocket::Provider>();
        if (provider.is_error()) {
          return Errno(fdio_status_to_errno(provider.error_value()));
        }
        struct ifreq* ifr = va_arg(va, struct ifreq*);
        auto response = provider->InterfaceIndexToName(static_cast<uint64_t>(ifr->ifr_ifindex));
        zx_status_t status = response.status();
        if (status != ZX_OK) {
          return Errno(fdio_status_to_errno(status));
        }
        auto const& result = response.value();
        if (result.is_error()) {
          if (result.error_value() == ZX_ERR_NOT_FOUND) {
            return Errno(ENODEV);
          }
          return Errno(fdio_status_to_errno(result.error_value()));
        }
        auto const& if_name = result.value()->name;
        const size_t len = std::min(if_name.size(), std::size(ifr->ifr_name));
        auto it = std::copy_n(if_name.begin(), len, std::begin(ifr->ifr_name));
        if (it != std::end(ifr->ifr_name)) {
          *it = 0;
        }
        return Errno(Errno::Ok);
      }
      case SIOCGIFINDEX: {
        auto& provider = get_client<fsocket::Provider>();
        if (provider.is_error()) {
          return Errno(fdio_status_to_errno(provider.error_value()));
        }
        struct ifreq* ifr = va_arg(va, struct ifreq*);
        fidl::StringView name(ifr->ifr_name, strnlen(ifr->ifr_name, sizeof(ifr->ifr_name) - 1));
        auto response = provider->InterfaceNameToIndex(name);
        zx_status_t status = response.status();
        if (status != ZX_OK) {
          if (status == ZX_ERR_INVALID_ARGS) {
            // FIDL calls will return ZX_ERR_INVALID_ARGS if the passed string
            // (`name` in this case) fails UTF-8 validation.
            return Errno(ENODEV);
          }
          return Errno(fdio_status_to_errno(status));
        }
        auto const& result = response.value();
        if (result.is_error()) {
          if (result.error_value() == ZX_ERR_NOT_FOUND) {
            return Errno(ENODEV);
          }
          return Errno(fdio_status_to_errno(result.error_value()));
        }
        ifr->ifr_ifindex = static_cast<int>(result.value()->index);
        return Errno(Errno::Ok);
      }
      case SIOCGIFFLAGS: {
        auto& provider = get_client<fsocket::Provider>();
        if (provider.is_error()) {
          return Errno(fdio_status_to_errno(provider.error_value()));
        }
        struct ifreq* ifr = va_arg(va, struct ifreq*);
        fidl::StringView name(ifr->ifr_name, strnlen(ifr->ifr_name, sizeof(ifr->ifr_name) - 1));
        auto response = provider->InterfaceNameToFlags(name);
        zx_status_t status = response.status();
        if (status != ZX_OK) {
          if (status == ZX_ERR_INVALID_ARGS) {
            // FIDL calls will return ZX_ERR_INVALID_ARGS if the passed string
            // (`name` in this case) fails UTF-8 validation.
            return Errno(ENODEV);
          }
          return Errno(fdio_status_to_errno(status));
        }
        auto const& result = response.value();
        if (result.is_error()) {
          if (result.error_value() == ZX_ERR_NOT_FOUND) {
            return Errno(ENODEV);
          }
          return Errno(fdio_status_to_errno(result.error_value()));
        }
        ifr->ifr_flags =
            static_cast<uint16_t>(result.value()->flags);  // NOLINT(bugprone-narrowing-conversions)
        return Errno(Errno::Ok);
      }
      case SIOCGIFCONF: {
        struct ifconf* ifc_ptr = va_arg(va, struct ifconf*);
        if (ifc_ptr == nullptr) {
          return Errno(EFAULT);
        }
        struct ifconf& ifc = *ifc_ptr;

        auto& provider = get_client<fsocket::Provider>();
        if (provider.is_error()) {
          return Errno(fdio_status_to_errno(provider.error_value()));
        }
        auto response = provider->GetInterfaceAddresses();
        zx_status_t status = response.status();
        if (status != ZX_OK) {
          return Errno(fdio_status_to_errno(status));
        }
        const auto& interfaces = response.value().interfaces;

        // If `ifc_req` is NULL, return the necessary buffer size in bytes for
        // receiving all available addresses in `ifc_len`.
        //
        // This allows the caller to determine the necessary buffer size
        // beforehand, and is the documented manual behavior.
        // See: https://man7.org/linux/man-pages/man7/netdevice.7.html
        if (ifc.ifc_req == nullptr) {
          int len = 0;
          for (const auto& iface : interfaces) {
            for (const auto& address : iface.addresses()) {
              if (address.addr.Which() == fnet::wire::IpAddress::Tag::kIpv4) {
                len += sizeof(struct ifreq);
              }
            }
          }
          ifc.ifc_len = len;
          return Errno(Errno::Ok);
        }

        struct ifreq* ifr = ifc.ifc_req;
        const auto buffer_full = [&] {
          return ifr + 1 > ifc.ifc_req + ifc.ifc_len / sizeof(struct ifreq);
        };
        for (const auto& iface : interfaces) {
          // Don't write past the caller-allocated buffer.
          // C++ doesn't support break labels, so we check this in both the inner
          // and outer loops.
          if (buffer_full()) {
            break;
          }
          // This should not happen, and would indicate a protocol error with
          // fuchsia.posix.socket/Provider.GetInterfaceAddresses.
          if (!iface.has_name() || !iface.has_addresses()) {
            continue;
          }

          const auto& if_name = iface.name();
          for (const auto& address : iface.addresses()) {
            // Don't write past the caller-allocated buffer.
            if (buffer_full()) {
              break;
            }
            // SIOCGIFCONF only returns interface addresses of the AF_INET (IPv4)
            // family for compatibility; this is the behavior documented in the
            // manual. See: https://man7.org/linux/man-pages/man7/netdevice.7.html
            const auto& addr = address.addr;
            if (addr.Which() != fnet::wire::IpAddress::Tag::kIpv4) {
              continue;
            }

            // Write interface name.
            const size_t len = std::min(if_name.size(), std::size(ifr->ifr_name));
            auto it = std::copy_n(if_name.begin(), len, std::begin(ifr->ifr_name));
            if (it != std::end(ifr->ifr_name)) {
              *it = 0;
            }

            // Write interface address.
            auto& s = *reinterpret_cast<struct sockaddr_in*>(&ifr->ifr_addr);
            const auto& ipv4 = addr.ipv4();
            s.sin_family = AF_INET;
            s.sin_port = 0;
            static_assert(sizeof(s.sin_addr) == sizeof(ipv4.addr));
            memcpy(&s.sin_addr, ipv4.addr.data(), sizeof(ipv4.addr));

            ifr++;
          }
        }
        ifc.ifc_len = static_cast<int>((ifr - ifc.ifc_req) * sizeof(struct ifreq));
        return Errno(Errno::Ok);
      }
      default:
        return zxio::posix_ioctl(req, va);
    }
  }

 protected:
  virtual fidl::WireSyncClient<typename T::FidlProtocol>& GetClient() = 0;
};

void recvmsg_populate_socketaddress(const fnet::wire::SocketAddress& fidl, void* addr,
                                    socklen_t& addr_len) {
  // Result address has invalid tag when it's not provided by the server (when the address
  // is not requested).
  // TODO(https://fxbug.dev/58503): Use better representation of nullable union when available.
  if (fidl.has_invalid_tag()) {
    return;
  }

  addr_len = zxio_fidl_to_sockaddr(fidl, addr, addr_len);
}

struct StreamSocket {
  using FidlProtocol = fsocket::StreamSocket;
};

struct DatagramSocket {
  using FidlProtocol = fsocket::DatagramSocket;
};

struct SynchronousDatagramSocket {
  using FidlProtocol = fsocket::SynchronousDatagramSocket;
  using FidlSockAddr = SocketAddress;
  using FidlSendControlData = fsocket::wire::DatagramSocketSendControlData;
  using zxio_type = zxio_synchronous_datagram_socket_t;

  static void recvmsg_populate_msgname(
      const fsocket::wire::SynchronousDatagramSocketRecvMsgResponse& response, void* addr,
      socklen_t& addr_len) {
    recvmsg_populate_socketaddress(response.addr, addr, addr_len);
  }

  static void handle_sendmsg_response(
      const fsocket::wire::SynchronousDatagramSocketSendMsgResponse& response,
      ssize_t expected_len) {
    // TODO(https://fxbug.dev/82346): Drop len from the response as SendMsg does
    // does not perform partial writes.
    ZX_DEBUG_ASSERT_MSG(response.len == expected_len, "got SendMsg(...) = %ld, want = %ld",
                        response.len, expected_len);
  }
};

struct RawSocket {
  using FidlProtocol = frawsocket::Socket;
  using FidlSockAddr = SocketAddress;
  using FidlSendControlData = fsocket::wire::NetworkSocketSendControlData;
  using zxio_type = zxio_raw_socket_t;

  static void recvmsg_populate_msgname(const frawsocket::wire::SocketRecvMsgResponse& response,
                                       void* addr, socklen_t& addr_len) {
    recvmsg_populate_socketaddress(response.addr, addr, addr_len);
  }

  static void handle_sendmsg_response(const frawsocket::wire::SocketSendMsgResponse& response,
                                      ssize_t expected_len) {
    // TODO(https://fxbug.dev/82346): Drop this method once DatagramSocket.SendMsg
    // no longer returns a length field.
  }
};

struct PacketSocket {
  using FidlProtocol = fpacketsocket::Socket;
  using FidlSockAddr = PacketInfo;
  using FidlSendControlData = fpacketsocket::wire::SendControlData;
  using zxio_type = zxio_packet_socket_t;

  static void recvmsg_populate_msgname(const fpacketsocket::wire::SocketRecvMsgResponse& response,
                                       void* addr, socklen_t& addr_len) {
    fidl::ObjectView view = response.packet_info;
    if (!view) {
      // The packet info field is not provided by the server (when it is not requested).
      return;
    }

    const fpacketsocket::wire::RecvPacketInfo& info = *view;

    sockaddr_ll sll = {
        .sll_family = AF_PACKET,
        .sll_protocol = htons(info.packet_info.protocol),
        .sll_ifindex = static_cast<int>(info.packet_info.interface_id),
        .sll_hatype = zxio_fidl_hwtype_to_arphrd(info.interface_type),
        .sll_pkttype = fidl_pkttype_to_pkttype(info.packet_type),
    };
    zxio_populate_from_fidl_hwaddr(info.packet_info.addr, sll);
    memcpy(addr, &sll, std::min(sizeof(sll), static_cast<size_t>(addr_len)));
    addr_len = sizeof(sll);
  }

  static void handle_sendmsg_response(const fpacketsocket::wire::SocketSendMsgResponse& response,
                                      ssize_t expected_len) {
    // TODO(https://fxbug.dev/82346): Drop this method once DatagramSocket.SendMsg
    // no longer returns a length field.
  }
};

template <typename R, typename = int>
struct FitxResultHasValue : std::false_type {};
template <typename R>
struct FitxResultHasValue<R, decltype(&R::value, 0)> : std::true_type {};
template <typename T, typename R>
typename std::enable_if<FitxResultHasValue<R>::value>::type HandleSendMsgResponse(const R& result,
                                                                                  size_t total) {
  T::handle_sendmsg_response(*result->value(), total);
}
template <typename T, typename R>
typename std::enable_if<!FitxResultHasValue<T>::value>::type HandleSendMsgResponse(const R& result,
                                                                                   size_t total) {}
template <typename T, typename = std::enable_if_t<std::is_same_v<T, SynchronousDatagramSocket> ||
                                                  std::is_same_v<T, RawSocket> ||
                                                  std::is_same_v<T, PacketSocket>>>
// inheritance is virtual to avoid multiple copies of `base_socket<T>` when derived classes
// inherit from `socket_with_event` and `network_socket`.
struct socket_with_event : virtual public base_socket<T> {
  static constexpr zx_signals_t kSignalIncoming = ZX_USER_SIGNAL_0;
  static constexpr zx_signals_t kSignalOutgoing = ZX_USER_SIGNAL_1;
  static constexpr zx_signals_t kSignalShutdownRead = ZX_USER_SIGNAL_4;
  static constexpr zx_signals_t kSignalShutdownWrite = ZX_USER_SIGNAL_5;

  void wait_begin(uint32_t events, zx_handle_t* handle, zx_signals_t* out_signals) override {
    *handle = zxio_socket_with_event().event.get();

    zx_signals_t signals = ZX_EVENTPAIR_PEER_CLOSED | this->kSignalError;
    if (events & POLLIN) {
      signals |= kSignalIncoming | kSignalShutdownRead;
    }
    if (events & POLLOUT) {
      signals |= kSignalOutgoing | kSignalShutdownWrite;
    }
    if (events & POLLRDHUP) {
      signals |= kSignalShutdownRead;
    }
    *out_signals = signals;
  }

  void wait_end(zx_signals_t signals, uint32_t* out_events) override {
    uint32_t events = 0;
    if (signals & (ZX_EVENTPAIR_PEER_CLOSED | kSignalIncoming | kSignalShutdownRead)) {
      events |= POLLIN;
    }
    if (signals & (ZX_EVENTPAIR_PEER_CLOSED | kSignalOutgoing | kSignalShutdownWrite)) {
      events |= POLLOUT;
    }
    if (signals & (ZX_EVENTPAIR_PEER_CLOSED | this->kSignalError)) {
      events |= POLLERR;
    }
    if (signals & (ZX_EVENTPAIR_PEER_CLOSED | kSignalShutdownRead)) {
      events |= POLLRDHUP;
    }
    *out_events = events;
  }

  zx_status_t recvmsg(struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    size_t datalen = 0;
    for (int i = 0; i < msg->msg_iovlen; ++i) {
      datalen += msg->msg_iov[i].iov_len;
    }

    bool want_addr = msg->msg_namelen != 0 && msg->msg_name != nullptr;
    bool want_cmsg = msg->msg_controllen != 0 && msg->msg_control != nullptr;
    auto response = GetClient()->RecvMsg(want_addr, static_cast<uint32_t>(datalen), want_cmsg,
                                         to_recvmsg_flags(flags));
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
      return ZX_OK;
    }
    *out_code = 0;

    T::recvmsg_populate_msgname(*result.value(), msg->msg_name, msg->msg_namelen);

    {
      auto const& out = result.value()->data;

      const uint8_t* data = out.begin();
      size_t remaining = out.count();
      for (int i = 0; remaining != 0 && i < msg->msg_iovlen; ++i) {
        iovec const& iov = msg->msg_iov[i];
        if (iov.iov_base != nullptr) {
          size_t actual = std::min(iov.iov_len, remaining);
          memcpy(iov.iov_base, data, actual);
          data += actual;
          remaining -= actual;
        } else if (iov.iov_len != 0) {
          *out_code = EFAULT;
          return ZX_OK;
        }
      }
      *out_actual = set_trunc_flags_and_return_out_actual(*msg, out.count() - remaining,
                                                          result.value()->truncated, flags);
    }

    if (want_cmsg) {
      FidlControlDataProcessor proc(msg->msg_control, msg->msg_controllen);
      // The synchronous datagram protocol returns all control messages found in the FIDL
      // response. This behavior is implemented using a "filter" that allows everything through.
      msg->msg_controllen =
          proc.Store(result.value()->control, RequestedCmsgSet::AllRequestedCmsgSet());
    } else {
      msg->msg_controllen = 0;
    }

    return ZX_OK;
  }

  zx_status_t sendmsg(const struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    typename T::FidlSockAddr addr;
    // Attempt to load socket address if either name or namelen is set.
    // If only one is set, it'll result in INVALID_ARGS.
    if (msg->msg_namelen != 0 || msg->msg_name != nullptr) {
      zx_status_t status =
          addr.LoadSockAddr(static_cast<struct sockaddr*>(msg->msg_name), msg->msg_namelen);
      if (status != ZX_OK) {
        return status;
      }
    }

    std::optional opt_total = total_iov_len(*msg);
    if (!opt_total.has_value()) {
      *out_code = EFAULT;
      return ZX_OK;
    }
    size_t total = opt_total.value();

    fidl::Arena allocator;
    fitx::result cmsg_result = ParseControlMessages<typename T::FidlSendControlData>(
        msg->msg_control, msg->msg_controllen, allocator);
    if (cmsg_result.is_error()) {
      *out_code = cmsg_result.error_value();
      return ZX_OK;
    }
    const typename T::FidlSendControlData& cdata = cmsg_result.value();

    std::vector<uint8_t> data;
    auto vec = fidl::VectorView<uint8_t>();
    switch (msg->msg_iovlen) {
      case 0: {
        break;
      }
      case 1: {
        const iovec& iov = *msg->msg_iov;
        vec = fidl::VectorView<uint8_t>::FromExternal(static_cast<uint8_t*>(iov.iov_base),
                                                      iov.iov_len);
        break;
      }
      default: {
        // TODO(https://fxbug.dev/84965): avoid this copy.
        data.reserve(total);
        for (int i = 0; i < msg->msg_iovlen; ++i) {
          const iovec& iov = msg->msg_iov[i];
          std::copy_n(static_cast<const uint8_t*>(iov.iov_base), iov.iov_len,
                      std::back_inserter(data));
        }
        vec = fidl::VectorView<uint8_t>::FromExternal(data);
      }
    }

    // TODO(https://fxbug.dev/58503): Use better representation of nullable union when
    // available. Currently just using a default-initialized union with an invalid tag.
    auto response = addr.WithFIDL([&](auto address) {
      return GetClient()->SendMsg(address, vec, cdata, to_sendmsg_flags(flags));
    });
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
      return ZX_OK;
    }
    HandleSendMsgResponse<T, decltype(result)>(result, total);

    *out_code = 0;
    // SendMsg does not perform partial writes.
    *out_actual = total;
    return ZX_OK;
  }

 protected:
  friend class fbl::internal::MakeRefCountedHelper<socket_with_event<T>>;
  friend class fbl::RefPtr<socket_with_event<T>>;

  socket_with_event() = default;
  ~socket_with_event() override = default;

  fidl::WireSyncClient<typename T::FidlProtocol>& GetClient() override {
    return zxio_socket_with_event().client;
  }

  typename T::zxio_type& zxio_socket_with_event() {
    return *reinterpret_cast<typename T::zxio_type*>(&base_socket<T>::zxio_storage().io);
  }
};

template <typename T,
          typename = std::enable_if_t<
              std::is_same_v<T, SynchronousDatagramSocket> || std::is_same_v<T, StreamSocket> ||
              std::is_same_v<T, RawSocket> || std::is_same_v<T, DatagramSocket>>>
// inheritance is virtual to avoid multiple copies of `base_socket<T>` when derived classes
// inherit from `network_socket` and `socket_with_event`.
struct network_socket : virtual public base_socket<T> {
  using base_socket<T>::GetClient;

  zx_status_t getsockopt(int level, int optname, void* optval, socklen_t* optlen,
                         int16_t* out_code) override {
    SockOptResult result =
        BaseNetworkSocket(GetClient()).getsockopt_fidl(level, optname, optval, optlen);
    *out_code = result.err;
    return result.status;
  }

  zx_status_t setsockopt(int level, int optname, const void* optval, socklen_t optlen,
                         int16_t* out_code) override {
    SockOptResult result =
        BaseNetworkSocket(GetClient()).setsockopt_fidl(level, optname, optval, optlen);
    *out_code = result.err;
    return result.status;
  }

  zx_status_t shutdown(int how, int16_t* out_code) override {
    return BaseNetworkSocket(GetClient()).shutdown(how, out_code);
  }
};

template <>
zx_status_t network_socket<RawSocket>::getsockopt(int level, int optname, void* optval,
                                                  socklen_t* optlen, int16_t* out_code) {
  SockOptResult result = [&]() {
    GetSockOptProcessor proc(optval, optlen);
    switch (level) {
      case SOL_ICMPV6:
        switch (optname) {
          case ICMP6_FILTER:
            return proc.Process(GetClient()->GetIcmpv6Filter(),
                                [](const auto& response) { return response.filter; });
        }
        break;
      case SOL_IPV6:
        switch (optname) {
          case IPV6_CHECKSUM:
            return proc.Process(GetClient()->GetIpv6Checksum(), [](const auto& response) {
              switch (response.config.Which()) {
                case frawsocket::wire::Ipv6ChecksumConfiguration::Tag::kDisabled:
                  return -1;
                case frawsocket::wire::Ipv6ChecksumConfiguration::Tag::kOffset:
                  return response.config.offset();
              };
            });
        }
        break;
      case SOL_IP:
        switch (optname) {
          case IP_HDRINCL:
            return proc.Process(GetClient()->GetIpHeaderIncluded(),
                                [](const auto& response) { return response.value; });
        }
        break;
    }
    return BaseNetworkSocket(GetClient()).getsockopt_fidl(level, optname, optval, optlen);
  }();
  *out_code = result.err;
  return result.status;
}

template <>
zx_status_t network_socket<RawSocket>::setsockopt(int level, int optname, const void* optval,
                                                  socklen_t optlen, int16_t* out_code) {
  SockOptResult result = [&]() {
    SetSockOptProcessor proc(optval, optlen);

    switch (level) {
      case SOL_ICMPV6:
        switch (optname) {
          case ICMP6_FILTER:
            return proc.Process<frawsocket::wire::Icmpv6Filter>(
                [this](frawsocket::wire::Icmpv6Filter value) {
                  return GetClient()->SetIcmpv6Filter(value);
                });
        }
        break;
      case SOL_IPV6:
        switch (optname) {
          case IPV6_CHECKSUM:
            return proc.Process<int32_t>([this](int32_t value) {
              frawsocket::wire::Ipv6ChecksumConfiguration config;

              if (value < 0) {
                config = frawsocket::wire::Ipv6ChecksumConfiguration::WithDisabled(
                    frawsocket::wire::Empty{});
              } else {
                config = frawsocket::wire::Ipv6ChecksumConfiguration::WithOffset(value);
              }

              return GetClient()->SetIpv6Checksum(config);
            });
        }
        break;
      case SOL_IP:
        switch (optname) {
          case IP_HDRINCL:
            return proc.Process<bool>(
                [this](bool value) { return GetClient()->SetIpHeaderIncluded(value); });
        }
        break;
    }
    return BaseNetworkSocket(GetClient()).setsockopt_fidl(level, optname, optval, optlen);
  }();
  *out_code = result.err;
  return result.status;
}

template <typename T, typename = std::enable_if_t<std::is_same_v<T, SynchronousDatagramSocket> ||
                                                  std::is_same_v<T, RawSocket>>>
struct network_socket_with_event : public socket_with_event<T>, public network_socket<T> {
 protected:
  friend class fbl::internal::MakeRefCountedHelper<network_socket_with_event<T>>;
  friend class fbl::RefPtr<network_socket_with_event<T>>;

  network_socket_with_event() = default;
  ~network_socket_with_event() override = default;
};

using synchronous_datagram_socket = network_socket_with_event<SynchronousDatagramSocket>;
using raw_socket = network_socket_with_event<RawSocket>;

}  // namespace fdio_internal

fdio_ptr fdio_synchronous_datagram_socket_allocate() {
  return fbl::MakeRefCounted<fdio_internal::synchronous_datagram_socket>();
}

fdio_ptr fdio_raw_socket_allocate() { return fbl::MakeRefCounted<fdio_internal::raw_socket>(); }

static zxio_datagram_socket_t& zxio_datagram_socket(zxio_t* io) {
  return *reinterpret_cast<zxio_datagram_socket_t*>(io);
}

static const zxio_datagram_socket_t& zxio_datagram_socket(const zxio_t* io) {
  return *reinterpret_cast<const zxio_datagram_socket_t*>(io);
}

namespace fdio_internal {

using ErrOrOutCode = zx::status<int16_t>;

template <typename T, typename = std::enable_if_t<std::is_same_v<T, DatagramSocket> ||
                                                  std::is_same_v<T, StreamSocket>>>
struct socket_with_zx_socket : public network_socket<T> {
 protected:
  virtual ErrOrOutCode GetError() = 0;

  std::optional<ErrOrOutCode> GetZxSocketWriteError(zx_status_t status) {
    switch (status) {
      case ZX_OK:
        return std::nullopt;
      case ZX_ERR_INVALID_ARGS:
        return zx::ok(static_cast<int16_t>(EFAULT));
      case ZX_ERR_BAD_STATE:
        __FALLTHROUGH;
      case ZX_ERR_PEER_CLOSED: {
        zx::status err = GetError();
        if (err.is_error()) {
          return zx::error(err.status_value());
        }
        if (int value = err.value(); value != 0) {
          return zx::ok(static_cast<int16_t>(value));
        }
        // Error was consumed.
        return zx::ok(static_cast<int16_t>(EPIPE));
      }
      default:
        return zx::error(status);
    }
  }

  virtual std::optional<ErrOrOutCode> GetZxSocketReadError(zx_status_t status) {
    switch (status) {
      case ZX_OK:
        return std::nullopt;
      case ZX_ERR_INVALID_ARGS:
        return zx::ok(static_cast<int16_t>(EFAULT));
      case ZX_ERR_BAD_STATE:
        __FALLTHROUGH;
      case ZX_ERR_PEER_CLOSED: {
        zx::status err = GetError();
        if (err.is_error()) {
          return zx::error(err.status_value());
        }
        return zx::ok(static_cast<int16_t>(err.value()));
      }
      default:
        return zx::error(status);
    }
  }
};

struct datagram_socket : public socket_with_zx_socket<DatagramSocket> {
  std::optional<ErrOrOutCode> GetZxSocketReadError(zx_status_t status) override {
    switch (status) {
      case ZX_ERR_BAD_STATE:
        // Datagram sockets return EAGAIN when a socket is read from after shutdown,
        // whereas stream sockets return zero bytes. Enforce this behavior here.
        return zx::ok(static_cast<int16_t>(EAGAIN));
      default:
        return socket_with_zx_socket<DatagramSocket>::GetZxSocketReadError(status);
    }
  }

  void wait_begin(uint32_t events, zx_handle_t* handle, zx_signals_t* out_signals) override {
    zxio_signals_t signals = ZXIO_SIGNAL_PEER_CLOSED;
    wait_begin_inner(events, signals, handle, out_signals);
    *out_signals |= kSignalError;
  }

  void wait_end(zx_signals_t zx_signals, uint32_t* out_events) override {
    zxio_signals_t signals;
    uint32_t events;
    wait_end_inner(zx_signals, &events, &signals);
    events |= zxio_signals_to_events(signals);
    if (zx_signals & kSignalError) {
      events |= POLLERR;
    }
    *out_events = events;
  }

  zx_status_t recvmsg(struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    // Before reading from the socket, we need to check for asynchronous
    // errors. Here, we combine this check with a cache lookup for the
    // requested control message set; when cmsgs are requested, this lets us
    // save a syscall.
    bool cmsg_requested = msg->msg_controllen != 0 && msg->msg_control != nullptr;
    RequestedCmsgCache::Result cache_result =
        cmsg_cache_.Get(socket_err_wait_item(), cmsg_requested, GetClient());
    if (!cache_result.is_ok()) {
      ErrOrOutCode err_value = cache_result.error_value();
      if (err_value.is_error()) {
        return err_value.status_value();
      }
      *out_code = err_value.value();
      return ZX_OK;
    }
    std::optional<RequestedCmsgSet> requested_cmsg_set = cache_result.value();

    zxio_flags_t zxio_flags = 0;
    if (flags & MSG_PEEK) {
      zxio_flags |= ZXIO_PEEK;
    }

    // Use stack allocated memory whenever the client-versioned `kRxUdpPreludeSize` is
    // at least as large as the server's.
    std::unique_ptr<uint8_t[]> heap_allocated_buf;
    uint8_t stack_allocated_buf[kRxUdpPreludeSize];
    uint8_t* buf = stack_allocated_buf;
    if (prelude_size().rx > kRxUdpPreludeSize) {
      heap_allocated_buf = std::make_unique<uint8_t[]>(prelude_size().rx);
      buf = heap_allocated_buf.get();
    }

    zx_iovec_t zx_iov[msg->msg_iovlen + 1];
    zx_iov[0] = {
        .buffer = buf,
        .capacity = prelude_size().rx,
    };

    size_t zx_iov_idx = 1;
    std::optional<size_t> fault_idx;
    {
      size_t idx = 0;
      for (int i = 0; i < msg->msg_iovlen; ++i) {
        iovec const& iov = msg->msg_iov[i];
        if (iov.iov_base != nullptr) {
          zx_iov[zx_iov_idx] = {
              .buffer = iov.iov_base,
              .capacity = iov.iov_len,
          };
          zx_iov_idx++;
          idx += iov.iov_len;
        } else if (iov.iov_len != 0) {
          fault_idx = idx;
          break;
        }
      }
    }

    size_t count_bytes_read;
    std::optional read_error = GetZxSocketReadError(
        zxio_readv(&zxio_storage().io, zx_iov, zx_iov_idx, zxio_flags, &count_bytes_read));
    if (read_error.has_value()) {
      zx::status err = read_error.value();
      if (!err.is_error()) {
        if (err.value() == 0) {
          *out_actual = 0;
        }
        *out_code = err.value();
      }
      return err.status_value();
    }

    if (count_bytes_read < prelude_size().rx) {
      *out_code = EIO;
      return ZX_OK;
    }

    fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta> decoded_meta =
        deserialize_recv_msg_meta(cpp20::span<uint8_t>(buf, prelude_size().rx));

    if (!decoded_meta.ok()) {
      *out_code = EIO;
      return ZX_OK;
    }

    const fuchsia_posix_socket::wire::RecvMsgMeta& meta = *decoded_meta.PrimaryObject();

    if (msg->msg_namelen != 0 && msg->msg_name != nullptr) {
      if (!meta.has_from()) {
        *out_code = EIO;
        return ZX_OK;
      }
      msg->msg_namelen = static_cast<socklen_t>(zxio_fidl_to_sockaddr(
          meta.from(), static_cast<struct sockaddr*>(msg->msg_name), msg->msg_namelen));
    }

    size_t payload_bytes_read = count_bytes_read - prelude_size().rx;
    if (payload_bytes_read > meta.payload_len()) {
      *out_code = EIO;
      return ZX_OK;
    }
    if (fault_idx.has_value() && meta.payload_len() > fault_idx.value()) {
      *out_code = EFAULT;
      return ZX_OK;
    }

    size_t truncated =
        meta.payload_len() > payload_bytes_read ? meta.payload_len() - payload_bytes_read : 0;
    *out_actual = set_trunc_flags_and_return_out_actual(*msg, payload_bytes_read, truncated, flags);

    if (cmsg_requested) {
      FidlControlDataProcessor proc(msg->msg_control, msg->msg_controllen);
      ZX_ASSERT_MSG(cmsg_requested == requested_cmsg_set.has_value(),
                    "cache lookup should return the RequestedCmsgSet iff it was requested");
      msg->msg_controllen = proc.Store(meta.control(), requested_cmsg_set.value());
    } else {
      msg->msg_controllen = 0;
    }

    *out_code = 0;
    return ZX_OK;
  }

  zx_status_t sendmsg(const struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    std::optional opt_total = total_iov_len(*msg);
    if (!opt_total.has_value()) {
      *out_code = EFAULT;
      return ZX_OK;
    }
    size_t total = opt_total.value();

    std::optional<SocketAddress> addr;
    // Attempt to load socket address if either name or namelen is set.
    // If only one is set, it'll result in INVALID_ARGS.
    if (msg->msg_namelen != 0 || msg->msg_name != nullptr) {
      zx_status_t status = addr.emplace().LoadSockAddr(static_cast<struct sockaddr*>(msg->msg_name),
                                                       msg->msg_namelen);
      if (status != ZX_OK) {
        return status;
      }
    }

    DestinationCache::Result result = dest_cache_.Get(addr, socket_err_wait_item(), GetClient());

    if (!result.is_ok()) {
      ErrOrOutCode err_value = result.error_value();
      if (err_value.is_error()) {
        return err_value.status_value();
      }
      *out_code = err_value.value();
      return ZX_OK;
    }

    if (result.value() < total) {
      *out_code = EMSGSIZE;
      return ZX_OK;
    }

    // Use stack allocated memory whenever the client-versioned `kTxUdpPreludeSize` is
    // at least as large as the server's.
    std::unique_ptr<uint8_t[]> heap_allocated_buf;
    uint8_t stack_allocated_buf[kTxUdpPreludeSize];
    uint8_t* buf = stack_allocated_buf;
    if (prelude_size().tx > kTxUdpPreludeSize) {
      heap_allocated_buf = std::make_unique<uint8_t[]>(prelude_size().tx);
      buf = heap_allocated_buf.get();
    }

    // TODO(https://fxbug.dev/103740): Avoid allocating into this arena.
    fidl::Arena alloc;
    fitx::result cmsg_result = ParseControlMessages<fsocket::wire::DatagramSocketSendControlData>(
        msg->msg_control, msg->msg_controllen, alloc);
    if (cmsg_result.is_error()) {
      *out_code = cmsg_result.error_value();
      return ZX_OK;
    }
    const fsocket::wire::DatagramSocketSendControlData& cdata = cmsg_result.value();
    auto meta_builder_with_cdata = [&alloc, &cdata]() {
      fidl::WireTableBuilder meta_builder = fuchsia_posix_socket::wire::SendMsgMeta::Builder(alloc);
      meta_builder.control(cdata);
      return meta_builder;
    };

    auto build_and_serialize =
        [this, &buf](fidl::WireTableBuilder<fsocket::wire::SendMsgMeta>& meta_builder) {
          fsocket::wire::SendMsgMeta meta = meta_builder.Build();
          return serialize_send_msg_meta(meta, cpp20::span<uint8_t>(buf, prelude_size().tx));
        };

    SerializeSendMsgMetaError serialize_err;
    if (addr.has_value()) {
      serialize_err = addr.value().WithFIDL(
          [&build_and_serialize, &meta_builder_with_cdata](fnet::wire::SocketAddress address) {
            fidl::WireTableBuilder meta_builder = meta_builder_with_cdata();
            meta_builder.to(address);
            return build_and_serialize(meta_builder);
          });
    } else {
      fidl::WireTableBuilder meta_builder = meta_builder_with_cdata();
      serialize_err = build_and_serialize(meta_builder);
    }

    if (serialize_err != SerializeSendMsgMetaErrorNone) {
      *out_code = EIO;
      return ZX_OK;
    }

    zx_iovec_t zx_iov[msg->msg_iovlen + 1];
    zx_iov[0] = {
        .buffer = buf,
        .capacity = prelude_size().tx,
    };

    size_t zx_iov_idx = 1;
    for (int i = 0; i < msg->msg_iovlen; ++i) {
      iovec const& iov = msg->msg_iov[i];
      if (iov.iov_base != nullptr) {
        zx_iov[zx_iov_idx] = {
            .buffer = iov.iov_base,
            .capacity = iov.iov_len,
        };
        zx_iov_idx++;
      }
    }

    size_t bytes_written;
    std::optional write_error = GetZxSocketWriteError(
        zxio_writev(&zxio_storage().io, zx_iov, zx_iov_idx, 0, &bytes_written));
    if (write_error.has_value()) {
      zx::status err = write_error.value();
      if (!err.is_error()) {
        *out_code = err.value();
      }
      return err.status_value();
    }

    size_t total_with_prelude = prelude_size().tx + total;
    if (bytes_written != total_with_prelude) {
      // Datagram writes should never be short.
      *out_code = EIO;
      return ZX_OK;
    }
    // A successful datagram socket write is never short, so we can assume all bytes
    // were written.
    *out_actual = total;
    *out_code = 0;
    return ZX_OK;
  }

 protected:
  friend class fbl::internal::MakeRefCountedHelper<datagram_socket>;
  friend class fbl::RefPtr<datagram_socket>;

  ~datagram_socket() override = default;

 private:
  zxio_datagram_socket_t& zxio_datagram_socket() {
    return ::zxio_datagram_socket(&zxio_storage().io);
  }

  const zxio_datagram_socket_t& zxio_datagram_socket() const {
    return ::zxio_datagram_socket(&zxio_storage().io);
  }

  const zxio_datagram_prelude_size_t& prelude_size() const {
    return zxio_datagram_socket().prelude_size;
  }

  zx_wait_item_t socket_err_wait_item() {
    return {
        .handle = zxio_datagram_socket().pipe.socket.get(),
        .waitfor = kSignalError,
    };
  }

  fidl::WireSyncClient<fsocket::DatagramSocket>& GetClient() override {
    return zxio_datagram_socket().client;
  }

  ErrOrOutCode GetError() override {
    std::optional err = GetErrorWithClient(GetClient());
    if (!err.has_value()) {
      return zx::ok(static_cast<int16_t>(0));
    }
    return err.value();
  }

  static std::optional<ErrOrOutCode> GetErrorWithClient(
      fidl::WireSyncClient<fsocket::DatagramSocket>& client) {
    const fidl::WireResult response = client->GetError();
    if (!response.ok()) {
      return zx::error(response.status());
    }
    const auto& result = response.value();
    if (result.is_error()) {
      return zx::ok(static_cast<int16_t>(result.error_value()));
    }
    return std::nullopt;
  }

  struct DestinationCache {
    // TODO(https://fxbug.dev/97260): Implement cache eviction strategy to avoid unbounded cache
    // growth.
   public:
    using Result = fitx::result<ErrOrOutCode, uint32_t>;
    Result Get(std::optional<SocketAddress>& addr, const zx_wait_item_t& err_wait_item,
               fidl::WireSyncClient<fsocket::DatagramSocket>& client) {
      // TODO(https://fxbug.dev/103653): Circumvent fast-path pessimization caused by lock
      // contention 1) between multiple fast paths and 2) between fast path and slow path.
      std::lock_guard lock(lock_);

      zx_wait_item_t wait_items[ZX_WAIT_MANY_MAX_ITEMS];
      constexpr uint32_t ERR_WAIT_ITEM_IDX = 0;
      wait_items[ERR_WAIT_ITEM_IDX] = err_wait_item;

      while (true) {
        std::optional<uint32_t> maximum_size;
        uint32_t num_wait_items = ERR_WAIT_ITEM_IDX + 1;
        const std::optional<SocketAddress>& addr_to_lookup = addr.has_value() ? addr : connected_;

        // NOTE: `addr_to_lookup` might not have a value if we're looking up the
        // connected addr for the first time. We still proceed with the syscall
        // to check for errors in that case (since the socket might have been
        // connected by another process).
        //
        // TODO(https://fxbug.dev/103655): Test errors are returned when connected
        // addr looked up for the first time.
        if (addr_to_lookup.has_value()) {
          if (auto it = cache_.find(addr_to_lookup.value()); it != cache_.end()) {
            const Value& value = it->second;
            ZX_ASSERT_MSG(value.eventpairs.size() + 1 <= ZX_WAIT_MANY_MAX_ITEMS,
                          "number of wait_items (%lu) exceeds maximum allowed (%zu)",
                          value.eventpairs.size() + 1, ZX_WAIT_MANY_MAX_ITEMS);
            for (const zx::eventpair& eventpair : value.eventpairs) {
              wait_items[num_wait_items] = {
                  .handle = eventpair.get(),
                  .waitfor = ZX_EVENTPAIR_PEER_CLOSED,
              };
              num_wait_items++;
            }
            maximum_size = value.maximum_size;
          }
        }

        zx_status_t status =
            zx::handle::wait_many(wait_items, num_wait_items, zx::time::infinite_past());

        switch (status) {
          case ZX_OK: {
            if (wait_items[ERR_WAIT_ITEM_IDX].pending & wait_items[ERR_WAIT_ITEM_IDX].waitfor) {
              std::optional err = GetErrorWithClient(client);
              if (err.has_value()) {
                return fitx::error(err.value());
              }
              continue;
            }
          } break;
          case ZX_ERR_TIMED_OUT: {
            if (maximum_size.has_value()) {
              return fitx::success(maximum_size.value());
            }
          } break;
          default:
            ErrOrOutCode err = zx::error(status);
            return fitx::error(err);
        }

        // TODO(https://fxbug.dev/103740): Avoid allocating into this arena.
        fidl::Arena alloc;
        const fidl::WireResult response = [&client, &alloc, &addr]() {
          if (addr.has_value()) {
            return addr.value().WithFIDL([&client, &alloc](fnet::wire::SocketAddress address) {
              fidl::WireTableBuilder request_builder =
                  fsocket::wire::DatagramSocketSendMsgPreflightRequest::Builder(alloc);
              request_builder.to(address);
              return client->SendMsgPreflight(request_builder.Build());
            });
          } else {
            fidl::WireTableBuilder request_builder =
                fsocket::wire::DatagramSocketSendMsgPreflightRequest::Builder(alloc);
            return client->SendMsgPreflight(request_builder.Build());
          }
        }();
        if (!response.ok()) {
          ErrOrOutCode err = zx::error(response.status());
          return fitx::error(err);
        }
        const auto& result = response.value();
        if (result.is_error()) {
          return fitx::error(zx::ok(static_cast<int16_t>(result.error_value())));
        }
        fsocket::wire::DatagramSocketSendMsgPreflightResponse& res = *result.value();

        std::optional<SocketAddress> returned_addr;
        if (!addr.has_value()) {
          if (res.has_to()) {
            returned_addr = SocketAddress::FromFidl(res.to());
          }
        }
        const std::optional<SocketAddress>& addr_to_store = addr.has_value() ? addr : returned_addr;

        if (!addr_to_store.has_value()) {
          return fitx::error(zx::ok(static_cast<int16_t>(EIO)));
        }

        if (!res.has_maximum_size() || !res.has_validity()) {
          return fitx::error(zx::ok(static_cast<int16_t>(EIO)));
        }

        std::vector<zx::eventpair> eventpairs;
        eventpairs.reserve(res.validity().count());
        std::move(res.validity().begin(), res.validity().end(), std::back_inserter(eventpairs));

        cache_[addr_to_store.value()] = {.eventpairs = std::move(eventpairs),
                                         .maximum_size = res.maximum_size()};

        if (!addr.has_value()) {
          connected_ = addr_to_store.value();
        }
      }
    }

   private:
    struct Value {
      std::vector<zx::eventpair> eventpairs;
      uint32_t maximum_size;
    };
    std::unordered_map<SocketAddress, Value> cache_ __TA_GUARDED(lock_);
    std::optional<SocketAddress> connected_ __TA_GUARDED(lock_);
    std::mutex lock_;
  };

  struct RequestedCmsgCache {
    // TODO(https://fxbug.dev/97260): Implement cache eviction strategy to avoid unbounded cache
    // growth.
   public:
    using Result = fitx::result<ErrOrOutCode, std::optional<RequestedCmsgSet>>;
    Result Get(zx_wait_item_t err_wait_item, bool get_requested_cmsg_set,
               fidl::WireSyncClient<fsocket::DatagramSocket>& client) {
      // TODO(https://fxbug.dev/103653): Circumvent fast-path pessimization caused by lock
      // contention between multiple fast paths.
      std::lock_guard lock(lock_);

      constexpr size_t MAX_WAIT_ITEMS = 2;
      zx_wait_item_t wait_items[MAX_WAIT_ITEMS];
      constexpr uint32_t ERR_WAIT_ITEM_IDX = 0;
      wait_items[ERR_WAIT_ITEM_IDX] = err_wait_item;
      std::optional<size_t> cmsg_idx;
      while (true) {
        uint32_t num_wait_items = ERR_WAIT_ITEM_IDX + 1;

        if (get_requested_cmsg_set && cache_.has_value()) {
          wait_items[num_wait_items] = {
              .handle = cache_.value().validity.get(),
              .waitfor = ZX_EVENTPAIR_PEER_CLOSED,
          };
          cmsg_idx = num_wait_items;
          num_wait_items++;
        }

        zx_status_t status =
            zx::handle::wait_many(wait_items, num_wait_items, zx::time::infinite_past());

        switch (status) {
          case ZX_OK: {
            const zx_wait_item_t& err_wait_item_ref = wait_items[ERR_WAIT_ITEM_IDX];
            if (err_wait_item_ref.pending & err_wait_item_ref.waitfor) {
              std::optional err = GetErrorWithClient(client);
              if (err.has_value()) {
                return fitx::error(err.value());
              }
              continue;
            }
            ZX_ASSERT_MSG(cmsg_idx.has_value(),
                          "wait_many({{.pending = %d, .waitfor = %d}}) == ZX_OK",
                          err_wait_item_ref.pending, err_wait_item_ref.waitfor);
            const zx_wait_item_t& cmsg_wait_item_ref = wait_items[cmsg_idx.value()];
            ZX_ASSERT_MSG(cmsg_wait_item_ref.pending & cmsg_wait_item_ref.waitfor,
                          "wait_many({{.pending = %d, .waitfor = %d}, {.pending = %d, .waitfor = "
                          "%d}}) == ZX_OK",
                          err_wait_item_ref.pending, err_wait_item_ref.waitfor,
                          cmsg_wait_item_ref.pending, cmsg_wait_item_ref.waitfor);
          } break;
          case ZX_ERR_TIMED_OUT: {
            if (!get_requested_cmsg_set) {
              return fitx::ok(std::nullopt);
            }
            if (cache_.has_value()) {
              return fitx::ok(cache_.value().requested_cmsg_set);
            }
          } break;
          default:
            ErrOrOutCode err = zx::error(status);
            return fitx::error(err);
        }

        const fidl::WireResult response = client->RecvMsgPostflight();
        if (!response.ok()) {
          ErrOrOutCode err = zx::error(response.status());
          return fitx::error(err);
        }
        const auto& result = response.value();
        if (result.is_error()) {
          return fitx::error(zx::ok(static_cast<int16_t>(result.error_value())));
        }
        fsocket::wire::DatagramSocketRecvMsgPostflightResponse& response_inner = *result.value();
        if (!response_inner.has_validity()) {
          return fitx::error(zx::ok(static_cast<int16_t>(EIO)));
        }
        cache_ = Value{
            .validity = std::move(response_inner.validity()),
            .requested_cmsg_set = RequestedCmsgSet(response_inner),
        };
      }
    }

   private:
    struct Value {
      zx::eventpair validity;
      RequestedCmsgSet requested_cmsg_set;
    };
    std::optional<Value> cache_ __TA_GUARDED(lock_);
    std::mutex lock_;
  };

  DestinationCache dest_cache_;
  RequestedCmsgCache cmsg_cache_;
};

}  // namespace fdio_internal

fdio_ptr fdio_datagram_socket_allocate() {
  return fbl::MakeRefCounted<fdio_internal::datagram_socket>();
}

static zxio_stream_socket_t& zxio_stream_socket(zxio_t* io) {
  return *reinterpret_cast<zxio_stream_socket_t*>(io);
}

namespace fdio_internal {

struct stream_socket : public socket_with_zx_socket<StreamSocket> {
  static constexpr zx_signals_t kSignalIncoming = ZX_USER_SIGNAL_0;
  static constexpr zx_signals_t kSignalConnected = ZX_USER_SIGNAL_3;

  void wait_begin(uint32_t events, zx_handle_t* handle, zx_signals_t* out_signals) override {
    zxio_signals_t signals = ZXIO_SIGNAL_PEER_CLOSED;

    auto [state, has_error] = GetState();
    switch (state) {
      case zxio_stream_socket_state_t::UNCONNECTED:
        // Stream sockets which are non-listening or unconnected do not have a potential peer
        // to generate any waitable signals, skip signal waiting and notify the caller of the
        // same.
        *out_signals = ZX_SIGNAL_NONE;
        return;
      case zxio_stream_socket_state_t::LISTENING:
        break;
      case zxio_stream_socket_state_t::CONNECTING:
        if (events & POLLIN) {
          signals |= ZXIO_SIGNAL_READABLE;
        }
        break;
      case zxio_stream_socket_state_t::CONNECTED:
        wait_begin_inner(events, signals, handle, out_signals);
        return;
    }

    if (events & POLLOUT) {
      signals |= ZXIO_SIGNAL_WRITE_DISABLED;
    }
    if (events & (POLLIN | POLLRDHUP)) {
      signals |= ZXIO_SIGNAL_READ_DISABLED;
    }

    zx_signals_t zx_signals = ZX_SIGNAL_NONE;
    zxio_wait_begin(&zxio_storage().io, signals, handle, &zx_signals);

    if (events & POLLOUT) {
      // signal when connect() operation is finished.
      zx_signals |= kSignalConnected;
    }
    if (events & POLLIN) {
      // signal when a listening socket gets an incoming connection.
      zx_signals |= kSignalIncoming;
    }
    *out_signals = zx_signals;
  }

  void wait_end(zx_signals_t zx_signals, uint32_t* out_events) override {
    zxio_signals_t signals = ZXIO_SIGNAL_NONE;
    uint32_t events = 0;

    bool use_inner;
    {
      std::lock_guard lock(zxio_stream_socket_state_lock());
      auto [state, has_error] = StateLocked();
      switch (state) {
        case zxio_stream_socket_state_t::UNCONNECTED:
          ZX_ASSERT_MSG(zx_signals == ZX_SIGNAL_NONE, "zx_signals=%s on unconnected socket",
                        std::bitset<sizeof(zx_signals)>(zx_signals).to_string().c_str());
          *out_events = POLLOUT | POLLHUP;
          return;

        case zxio_stream_socket_state_t::LISTENING:
          if (zx_signals & kSignalIncoming) {
            events |= POLLIN;
          }
          use_inner = false;
          break;
        case zxio_stream_socket_state_t::CONNECTING:
          if (zx_signals & kSignalConnected) {
            zxio_stream_socket_state() = zxio_stream_socket_state_t::CONNECTED;
            events |= POLLOUT;
          }
          zx_signals &= ~kSignalConnected;
          use_inner = false;
          break;
        case zxio_stream_socket_state_t::CONNECTED:
          use_inner = true;
          break;
      }
    }

    if (use_inner) {
      wait_end_inner(zx_signals, &events, &signals);
    } else {
      zxio_wait_end(&zxio_storage().io, zx_signals, &signals);
    }

    events |= zxio_signals_to_events(signals);
    *out_events = events;
  }

  zx_status_t recvmsg(struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    std::optional preflight = Preflight(ENOTCONN);
    if (preflight.has_value()) {
      ErrOrOutCode err = preflight.value();
      if (err.is_error()) {
        return err.status_value();
      }
      *out_code = err.value();
      return ZX_OK;
    }

    std::optional read_error = GetZxSocketReadError(recvmsg_inner(msg, flags, out_actual));
    if (read_error.has_value()) {
      zx::status err = read_error.value();
      if (!err.is_error()) {
        *out_code = err.value();
        if (err.value() == 0) {
          *out_actual = 0;
        }
      }
      return err.status_value();
    }
    *out_code = 0;
    return ZX_OK;
  }

  zx_status_t sendmsg(const struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    std::optional preflight = Preflight(EPIPE);
    if (preflight.has_value()) {
      ErrOrOutCode err = preflight.value();
      if (err.is_error()) {
        return err.status_value();
      }
      *out_code = err.value();
      return ZX_OK;
    }

    // Fuchsia does not support control messages on stream sockets. But we still parse the buffer
    // to check that it is valid.
    fidl::Arena allocator;
    fitx::result cmsg_result = ParseControlMessages<fsocket::wire::SocketSendControlData>(
        msg->msg_control, msg->msg_controllen, allocator);
    if (cmsg_result.is_error()) {
      *out_code = cmsg_result.error_value();
      return ZX_OK;
    }

    std::optional write_error = GetZxSocketWriteError(sendmsg_inner(msg, flags, out_actual));
    if (write_error.has_value()) {
      zx::status err = write_error.value();
      if (!err.is_error()) {
        *out_code = err.value();
      }
      return err.status_value();
    }
    *out_code = 0;
    return ZX_OK;
  }

 private:
  zxio_stream_socket_t& zxio_stream_socket() { return ::zxio_stream_socket(&zxio_storage().io); }
  zxio_stream_socket_state_t& zxio_stream_socket_state() { return zxio_stream_socket().state; }
  std::mutex& zxio_stream_socket_state_lock() { return zxio_stream_socket().state_lock; }

  std::optional<ErrOrOutCode> Preflight(int fallback) {
    auto [state, has_error] = GetState();
    if (has_error) {
      zx::status err = GetError();
      if (err.is_error()) {
        return err.take_error();
      }
      if (int16_t value = err.value(); value != 0) {
        return zx::ok(value);
      }
      // Error was consumed.
    }

    switch (state) {
      case zxio_stream_socket_state_t::UNCONNECTED:
        __FALLTHROUGH;
      case zxio_stream_socket_state_t::LISTENING:
        return zx::ok(static_cast<int16_t>(fallback));
      case zxio_stream_socket_state_t::CONNECTING:
        if (!has_error) {
          return zx::ok(static_cast<int16_t>(EAGAIN));
        }
        // There's an error on the socket, we will discover it when we perform our I/O.
        __FALLTHROUGH;
      case zxio_stream_socket_state_t::CONNECTED:
        return std::nullopt;
    }
  }

  ErrOrOutCode GetError() override {
    fidl::WireResult response = GetClient()->GetError();
    if (!response.ok()) {
      return zx::error(response.status());
    }
    const auto& result = response.value();
    if (result.is_error()) {
      return zx::ok(static_cast<int16_t>(result.error_value()));
    }
    return zx::ok(static_cast<int16_t>(0));
  }

  fidl::WireSyncClient<fsocket::StreamSocket>& GetClient() override {
    return zxio_stream_socket().client;
  }

  std::pair<zxio_stream_socket_state_t, bool> StateLocked()
      __TA_REQUIRES(zxio_stream_socket_state_lock()) {
    switch (zxio_stream_socket_state()) {
      case zxio_stream_socket_state_t::UNCONNECTED:
        __FALLTHROUGH;
      case zxio_stream_socket_state_t::LISTENING:
        return std::make_pair(zxio_stream_socket_state(), false);
      case zxio_stream_socket_state_t::CONNECTING: {
        zx_signals_t observed;
        zx_status_t status = zxio_stream_socket().pipe.socket.wait_one(
            kSignalConnected, zx::time::infinite_past(), &observed);
        switch (status) {
          case ZX_OK:
            if (observed & kSignalConnected) {
              zxio_stream_socket_state() = zxio_stream_socket_state_t::CONNECTED;
            }
            __FALLTHROUGH;
          case ZX_ERR_TIMED_OUT:
            return std::make_pair(zxio_stream_socket_state(), observed & ZX_SOCKET_PEER_CLOSED);
          default:
            ZX_PANIC("ASSERT FAILED at (%s:%d): status=%s\n", __FILE__, __LINE__,
                     zx_status_get_string(status));
        }
        break;
      }
      case zxio_stream_socket_state_t::CONNECTED:
        return std::make_pair(zxio_stream_socket_state(), false);
    }
  }

  std::pair<zxio_stream_socket_state_t, bool> GetState()
      __TA_EXCLUDES(zxio_stream_socket_state_lock()) {
    std::lock_guard lock(zxio_stream_socket_state_lock());
    return StateLocked();
  }

 protected:
  friend class fbl::internal::MakeRefCountedHelper<stream_socket>;
  friend class fbl::RefPtr<stream_socket>;

  ~stream_socket() override = default;
};

}  // namespace fdio_internal

fdio_ptr fdio_stream_socket_allocate() {
  return fbl::MakeRefCounted<fdio_internal::stream_socket>();
}

namespace fdio_internal {

struct packet_socket : public socket_with_event<PacketSocket> {
  SockOptResult getsockopt_fidl(int level, int optname, void* optval, socklen_t* optlen) {
    switch (level) {
      case SOL_SOCKET:
        return BaseSocket(GetClient()).get_solsocket_sockopt_fidl(optname, optval, optlen);
      default:
        return SockOptResult::Errno(EPROTONOSUPPORT);
    }
  }

  zx_status_t getsockopt(int level, int optname, void* optval, socklen_t* optlen,
                         int16_t* out_code) override {
    SockOptResult result = getsockopt_fidl(level, optname, optval, optlen);
    *out_code = result.err;
    return result.status;
  }

  SockOptResult setsockopt_fidl(int level, int optname, const void* optval, socklen_t optlen) {
    switch (level) {
      case SOL_SOCKET:
        return BaseSocket(GetClient()).set_solsocket_sockopt_fidl(optname, optval, optlen);
      default:
        return SockOptResult::Errno(EPROTONOSUPPORT);
    }
  }

  zx_status_t setsockopt(int level, int optname, const void* optval, socklen_t optlen,
                         int16_t* out_code) override {
    SockOptResult result = setsockopt_fidl(level, optname, optval, optlen);
    *out_code = result.err;
    return result.status;
  }

  zx_status_t shutdown(int how, int16_t* out_code) override { return ZX_ERR_NOT_SUPPORTED; }

 protected:
  friend class fbl::internal::MakeRefCountedHelper<packet_socket>;
  friend class fbl::RefPtr<packet_socket>;

  packet_socket() = default;
  ~packet_socket() override = default;
};

}  // namespace fdio_internal

fdio_ptr fdio_packet_socket_allocate() {
  return fbl::MakeRefCounted<fdio_internal::packet_socket>();
}
