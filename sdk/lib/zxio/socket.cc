// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.posix.socket.packet/cpp/wire.h>
#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/fidl/cpp/wire/string_view.h>
#include <lib/fit/result.h>
#include <lib/zx/socket.h>
#include <lib/zxio/cpp/cmsg.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/cpp/socket_address.h>
#include <lib/zxio/cpp/transitional.h>
#include <lib/zxio/cpp/vector.h>
#include <lib/zxio/null.h>
#include <netinet/icmp6.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstring>
#include <type_traits>

#include <safemath/safe_conversions.h>

#include "sdk/lib/zxio/private.h"

namespace fio = fuchsia_io;
namespace fsocket = fuchsia_posix_socket;
namespace frawsocket = fuchsia_posix_socket_raw;
namespace fpacketsocket = fuchsia_posix_socket_packet;
namespace fnet = fuchsia_net;

namespace {

uint16_t fidl_protoassoc_to_protocol(const fpacketsocket::wire::ProtocolAssociation& protocol) {
  // protocol has an invalid tag when it's not provided by the server (when the socket is not
  // associated).
  //
  // TODO(https://fxbug.dev/58503): Use better representation of nullable union when available.
  if (protocol.has_invalid_tag()) {
    return 0;
  }

  switch (protocol.Which()) {
    case fpacketsocket::wire::ProtocolAssociation::Tag::kAll:
      return ETH_P_ALL;
    case fpacketsocket::wire::ProtocolAssociation::Tag::kSpecified:
      return protocol.specified();
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

template <typename Client,
          typename = std::enable_if_t<
              std::is_same_v<Client, fidl::WireSyncClient<fsocket::SynchronousDatagramSocket>> ||
              std::is_same_v<Client, fidl::WireSyncClient<fsocket::DatagramSocket>> ||
              std::is_same_v<Client, fidl::WireSyncClient<fsocket::StreamSocket>> ||
              std::is_same_v<Client, fidl::WireSyncClient<frawsocket::Socket>> ||
              std::is_same_v<Client, fidl::WireSyncClient<fpacketsocket::Socket>>>>
class base_socket {
  static_assert(std::is_same_v<Client, fidl::WireSyncClient<fsocket::SynchronousDatagramSocket>> ||
                std::is_same_v<Client, fidl::WireSyncClient<fsocket::DatagramSocket>> ||
                std::is_same_v<Client, fidl::WireSyncClient<fsocket::StreamSocket>> ||
                std::is_same_v<Client, fidl::WireSyncClient<frawsocket::Socket>> ||
                std::is_same_v<Client, fidl::WireSyncClient<fpacketsocket::Socket>>);

 public:
  explicit base_socket(Client& client) : client_(client) {}

  Client& client() { return client_; }

  zx_status_t CloseSocket() {
    const fidl::WireResult result = client_->Close();
    if (!result.ok()) {
      return result.status();
    }
    const auto& response = result.value();
    if (response.is_error()) {
      return response.error_value();
    }
    return client_.client_end().channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                                                   nullptr);
  }

  zx_status_t CloneSocket(zx_handle_t* out_handle) {
    zx::status endpoints = fidl::CreateEndpoints<fio::Node>();
    if (endpoints.is_error()) {
      return endpoints.status_value();
    }
    zx_status_t status =
        client_
            ->Clone2(fidl::ServerEnd<fuchsia_unknown::Cloneable>{endpoints->server.TakeChannel()})
            .status();
    if (status != ZX_OK) {
      return status;
    }
    *out_handle = endpoints->client.channel().release();
    return ZX_OK;
  }

  SockOptResult get_solsocket_sockopt_fidl(int optname, void* optval, socklen_t* optlen) {
    GetSockOptProcessor proc(optval, optlen);
    switch (optname) {
      case SO_TYPE:
        if constexpr (std::is_same_v<Client, fidl::WireSyncClient<fsocket::DatagramSocket>> ||
                      std::is_same_v<Client,
                                     fidl::WireSyncClient<fsocket::SynchronousDatagramSocket>>) {
          return proc.StoreOption<int32_t>(SOCK_DGRAM);
        }
        if constexpr (std::is_same_v<Client, fidl::WireSyncClient<fsocket::StreamSocket>>) {
          return proc.StoreOption<int32_t>(SOCK_STREAM);
        }
        if constexpr (std::is_same_v<Client, fidl::WireSyncClient<frawsocket::Socket>>) {
          return proc.StoreOption<int32_t>(SOCK_RAW);
        }
        if constexpr (std::is_same_v<Client, fidl::WireSyncClient<fpacketsocket::Socket>>) {
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
        if constexpr (std::is_same_v<Client, fidl::WireSyncClient<fpacketsocket::Socket>>) {
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
        if constexpr (std::is_same_v<Client, fidl::WireSyncClient<fsocket::DatagramSocket>> ||
                      std::is_same_v<Client,
                                     fidl::WireSyncClient<fsocket::SynchronousDatagramSocket>>) {
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
        if constexpr (std::is_same_v<Client, fidl::WireSyncClient<fsocket::StreamSocket>>) {
          return proc.Process(client()->GetInfo(), [](const auto& response) {
            switch (response.proto) {
              case fsocket::wire::StreamSocketProtocol::kTcp:
                return IPPROTO_TCP;
            }
          });
        }
        if constexpr (std::is_same_v<Client, fidl::WireSyncClient<frawsocket::Socket>>) {
          return proc.Process(client()->GetInfo(), [](const auto& response) {
            switch (response.proto.Which()) {
              case frawsocket::wire::ProtocolAssociation::Tag::kUnassociated:
                return IPPROTO_RAW;
              case frawsocket::wire::ProtocolAssociation::Tag::kAssociated:
                return static_cast<int>(response.proto.associated());
            }
          });
        }
        if constexpr (std::is_same_v<Client, fidl::WireSyncClient<fpacketsocket::Socket>>) {
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
  Client& client_;
};

template <typename T,
          typename = std::enable_if_t<
              std::is_same_v<T, fidl::WireSyncClient<fsocket::SynchronousDatagramSocket>> ||
              std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>> ||
              std::is_same_v<T, fidl::WireSyncClient<frawsocket::Socket>> ||
              std::is_same_v<T, fidl::WireSyncClient<fsocket::DatagramSocket>>>>
struct network_socket : public base_socket<T> {
  static_assert(std::is_same_v<T, fidl::WireSyncClient<fsocket::SynchronousDatagramSocket>> ||
                std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>> ||
                std::is_same_v<T, fidl::WireSyncClient<frawsocket::Socket>> ||
                std::is_same_v<T, fidl::WireSyncClient<fsocket::DatagramSocket>>);

 public:
  using base_socket = base_socket<T>;
  using base_socket::client;

  explicit network_socket(T& client) : base_socket(client) {}

  zx_status_t bind(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    SocketAddress fidl_addr;
    zx_status_t status = fidl_addr.LoadSockAddr(addr, addrlen);
    if (status != ZX_OK) {
      return status;
    }

    auto response = fidl_addr.WithFIDL(
        [this](fnet::wire::SocketAddress address) { return client()->Bind(address); });
    status = response.status();
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

  zx_status_t connect(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    // If address is AF_UNSPEC we should call disconnect.
    if (addr->sa_family == AF_UNSPEC) {
      auto response = client()->Disconnect();
      zx_status_t status = response.status();
      if (status != ZX_OK) {
        return status;
      }
      const auto& result = response.value();
      if (result.is_error()) {
        *out_code = static_cast<int16_t>(result.error_value());
      } else {
        *out_code = 0;
      }
      return ZX_OK;
    }

    SocketAddress fidl_addr;
    zx_status_t status = fidl_addr.LoadSockAddr(addr, addrlen);
    if (status != ZX_OK) {
      return status;
    }

    auto response = fidl_addr.WithFIDL(
        [this](fnet::wire::SocketAddress address) { return client()->Connect(address); });
    status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
    } else {
      *out_code = 0;
    }
    return ZX_OK;
  }

  template <typename R>
  zx_status_t getname(R&& response, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
      return ZX_OK;
    }
    if (addrlen == nullptr || (*addrlen != 0 && addr == nullptr)) {
      *out_code = EFAULT;
      return ZX_OK;
    }
    *out_code = 0;
    auto const& out = result.value()->addr;
    *addrlen = zxio_fidl_to_sockaddr(out, addr, *addrlen);
    return ZX_OK;
  }

  zx_status_t getsockname(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return getname(client()->GetSockName(), addr, addrlen, out_code);
  }

  zx_status_t getpeername(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return getname(client()->GetPeerName(), addr, addrlen, out_code);
  }

  SockOptResult getsockopt_fidl(int level, int optname, void* optval, socklen_t* optlen) {
    if (optval == nullptr || optlen == nullptr) {
      return SockOptResult::Errno(EFAULT);
    }

    GetSockOptProcessor proc(optval, optlen);
    switch (level) {
      case SOL_SOCKET:
        return base_socket::get_solsocket_sockopt_fidl(optname, optval, optlen);
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
        return base_socket::set_solsocket_sockopt_fidl(optname, optval, optlen);
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

  zx_status_t shutdown(zxio_shutdown_options_t options, int16_t* out_code) {
    using fsocket::wire::ShutdownMode;
    ShutdownMode mode;
    if (options == ZXIO_SHUTDOWN_OPTIONS_READ) {
      mode = ShutdownMode::kRead;
    } else if (options == ZXIO_SHUTDOWN_OPTIONS_WRITE) {
      mode = ShutdownMode::kWrite;
    } else if (options == (ZXIO_SHUTDOWN_OPTIONS_READ | ZXIO_SHUTDOWN_OPTIONS_WRITE)) {
      mode = ShutdownMode::kRead | ShutdownMode::kWrite;
    } else {
      return ZX_ERR_INVALID_ARGS;
    }

    const auto response = client()->Shutdown(mode);
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    const auto& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
      return ZX_OK;
    }
    *out_code = 0;
    return ZX_OK;
  }
};

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

uint16_t fidl_hwtype_to_arphrd(const fpacketsocket::wire::HardwareType type) {
  switch (type) {
    case fpacketsocket::wire::HardwareType::kNetworkOnly:
      return ARPHRD_NONE;
    case fpacketsocket::wire::HardwareType::kEthernet:
      return ARPHRD_ETHER;
    case fpacketsocket::wire::HardwareType::kLoopback:
      return ARPHRD_LOOPBACK;
  }
}

void populate_from_fidl_hwaddr(const fpacketsocket::wire::HardwareAddress& addr, sockaddr_ll& s) {
  switch (addr.Which()) {
    case fpacketsocket::wire::HardwareAddress::Tag::kUnknown:
      // The server is newer than us and sending a variant we don't understand.
      __FALLTHROUGH;
    case fpacketsocket::wire::HardwareAddress::Tag::kNone:
      s.sll_halen = 0;
      break;
    case fpacketsocket::wire::HardwareAddress::Tag::kEui48: {
      const fnet::wire::MacAddress& eui48 = addr.eui48();
      static_assert(std::size(decltype(s.sll_addr){}) == decltype(eui48.octets)::size() + 2);
      std::copy(eui48.octets.begin(), eui48.octets.end(), std::begin(s.sll_addr));
      s.sll_halen = decltype(eui48.octets)::size();
    } break;
  }
}

struct SynchronousDatagramSocket {
  using FidlSockAddr = SocketAddress;
  using FidlSendControlData = fsocket::wire::DatagramSocketSendControlData;
  using Storage = zxio_synchronous_datagram_socket_t;

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
  using FidlSockAddr = SocketAddress;
  using FidlSendControlData = fsocket::wire::NetworkSocketSendControlData;
  using Storage = zxio_raw_socket_t;

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
  using FidlSockAddr = PacketInfo;
  using FidlSendControlData = fpacketsocket::wire::SendControlData;
  using Storage = zxio_packet_socket_t;

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
        .sll_hatype = fidl_hwtype_to_arphrd(info.interface_type),
        .sll_pkttype = fidl_pkttype_to_pkttype(info.packet_type),
    };
    populate_from_fidl_hwaddr(info.packet_info.addr, sll);
    memcpy(addr, &sll, std::min(sizeof(sll), static_cast<size_t>(addr_len)));
    addr_len = sizeof(sll);
  }

  static void handle_sendmsg_response(const fpacketsocket::wire::SocketSendMsgResponse& response,
                                      ssize_t expected_len) {
    // TODO(https://fxbug.dev/82346): Drop this method once DatagramSocket.SendMsg
    // no longer returns a length field.
  }
};

}  // namespace

std::optional<size_t> zxio_total_iov_len(const struct msghdr& msg) {
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

size_t zxio_set_trunc_flags_and_return_out_actual(struct msghdr& msg, size_t written,
                                                  size_t truncated, int flags) {
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

namespace {

template <typename R, typename = int>
struct FitResultHasValue : std::false_type {};
template <typename R>
struct FitResultHasValue<R, decltype(&R::value, 0)> : std::true_type {};
template <typename T, typename R>
typename std::enable_if<FitResultHasValue<R>::value>::type HandleSendMsgResponse(const R& result,
                                                                                 size_t total) {
  T::handle_sendmsg_response(*result->value(), total);
}
template <typename T, typename R>
typename std::enable_if<!FitResultHasValue<T>::value>::type HandleSendMsgResponse(const R& result,
                                                                                  size_t total) {}

template <typename T, typename = std::enable_if_t<std::is_same_v<T, SynchronousDatagramSocket> ||
                                                  std::is_same_v<T, RawSocket> ||
                                                  std::is_same_v<T, PacketSocket>>>
struct socket_with_event {
 public:
  explicit socket_with_event(
      typename fidl::WireSyncClient<typename T::Storage::FidlProtocol>& client)
      : client_(client) {}

  zx_status_t recvmsg(struct msghdr* msg, int flags, size_t* out_actual, int16_t* out_code) {
    size_t datalen = 0;
    for (int i = 0; i < msg->msg_iovlen; ++i) {
      datalen += msg->msg_iov[i].iov_len;
    }

    bool want_addr = msg->msg_namelen != 0 && msg->msg_name != nullptr;
    bool want_cmsg = msg->msg_controllen != 0 && msg->msg_control != nullptr;
    auto response = client_->RecvMsg(want_addr, static_cast<uint32_t>(datalen), want_cmsg,
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
      *out_actual = zxio_set_trunc_flags_and_return_out_actual(*msg, out.count() - remaining,
                                                               result.value()->truncated, flags);
    }

    if (want_cmsg) {
      FidlControlDataProcessor proc(msg->msg_control, msg->msg_controllen);
      // The synchronous datagram protocol returns all control messages found in the FIDL
      // response. This behavior is implemented using a "filter" that allows everything
      // through.
      msg->msg_controllen =
          proc.Store(result.value()->control, RequestedCmsgSet::AllRequestedCmsgSet());
    } else {
      msg->msg_controllen = 0;
    }

    return ZX_OK;
  }

  zx_status_t sendmsg(const struct msghdr* msg, int flags, size_t* out_actual, int16_t* out_code) {
    // TODO(https://fxbug.dev/110570) Add tests with msg as nullptr.
    if (msg == nullptr) {
      *out_code = EFAULT;
      return ZX_OK;
    }
    const msghdr& msghdr_ref = *msg;
    typename T::FidlSockAddr addr;
    // Attempt to load socket address if either name or namelen is set.
    // If only one is set, it'll result in INVALID_ARGS.
    if (msghdr_ref.msg_namelen != 0 || msghdr_ref.msg_name != nullptr) {
      zx_status_t status =
          addr.LoadSockAddr(static_cast<struct sockaddr*>(msg->msg_name), msg->msg_namelen);
      if (status != ZX_OK) {
        return status;
      }
    }

    std::optional opt_total = zxio_total_iov_len(msghdr_ref);
    if (!opt_total.has_value()) {
      *out_code = EFAULT;
      return ZX_OK;
    }
    size_t total = opt_total.value();

    fidl::Arena allocator;
    fit::result cmsg_result =
        ParseControlMessages<typename T::FidlSendControlData>(allocator, msghdr_ref);
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
      return client_->SendMsg(address, vec, cdata, to_sendmsg_flags(flags));
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

 private:
  typename fidl::WireSyncClient<typename T::Storage::FidlProtocol>& client_;
};

}  // namespace

static constexpr zxio_ops_t zxio_default_socket_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.connect = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    *out_code = EOPNOTSUPP;
    return ZX_OK;
  };
  ops.listen = [](zxio_t* io, int backlog, int16_t* out_code) {
    *out_code = EOPNOTSUPP;
    return ZX_OK;
  };
  ops.accept = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                  zxio_storage_t* out_storage, int16_t* out_code) {
    *out_code = EOPNOTSUPP;
    return ZX_OK;
  };
  ops.getpeername = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    *out_code = EOPNOTSUPP;
    return ZX_OK;
  };
  ops.shutdown = [](zxio_t* io, zxio_shutdown_options_t options, int16_t* out_code) {
    *out_code = EOPNOTSUPP;
    return ZX_OK;
  };
  return ops;
}();

static zxio_synchronous_datagram_socket_t& zxio_synchronous_datagram_socket(zxio_t* io) {
  return *reinterpret_cast<zxio_synchronous_datagram_socket_t*>(io);
}

static constexpr zxio_ops_t zxio_synchronous_datagram_socket_ops = []() {
  zxio_ops_t ops = zxio_default_socket_ops;
  ops.close = [](zxio_t* io) {
    zxio_synchronous_datagram_socket_t& zs = zxio_synchronous_datagram_socket(io);
    zx_status_t status = ZX_OK;
    if (zs.client.is_valid()) {
      status = base_socket(zs.client).CloseSocket();
    }
    zs.~zxio_synchronous_datagram_socket_t();
    return status;
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    if (out_handle == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    *out_handle =
        zxio_synchronous_datagram_socket(io).client.TakeClientEnd().TakeChannel().release();
    return ZX_OK;
  };
  ops.borrow = [](zxio_t* io, zx_handle_t* out_handle) {
    *out_handle =
        zxio_synchronous_datagram_socket(io).client.client_end().borrow().channel()->get();
    return ZX_OK;
  };
  ops.clone = [](zxio_t* io, zx_handle_t* out_handle) {
    zxio_synchronous_datagram_socket_t& zs = zxio_synchronous_datagram_socket(io);
    zx_status_t status = base_socket(zs.client).CloneSocket(out_handle);
    return status;
  };
  ops.bind = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    return network_socket(zxio_synchronous_datagram_socket(io).client)
        .bind(addr, addrlen, out_code);
  };
  ops.connect = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    return network_socket(zxio_synchronous_datagram_socket(io).client)
        .connect(addr, addrlen, out_code);
  };
  ops.getsockname = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return network_socket(zxio_synchronous_datagram_socket(io).client)
        .getsockname(addr, addrlen, out_code);
  };
  ops.getpeername = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return network_socket(zxio_synchronous_datagram_socket(io).client)
        .getpeername(addr, addrlen, out_code);
  };
  ops.getsockopt = [](zxio_t* io, int level, int optname, void* optval, socklen_t* optlen,
                      int16_t* out_code) {
    SockOptResult result = network_socket(zxio_synchronous_datagram_socket(io).client)
                               .getsockopt_fidl(level, optname, optval, optlen);
    *out_code = result.err;
    return result.status;
  };
  ops.setsockopt = [](zxio_t* io, int level, int optname, const void* optval, socklen_t optlen,
                      int16_t* out_code) {
    SockOptResult result = network_socket(zxio_synchronous_datagram_socket(io).client)
                               .setsockopt_fidl(level, optname, optval, optlen);
    *out_code = result.err;
    return result.status;
  };
  ops.recvmsg = [](zxio_t* io, struct msghdr* msg, int flags, size_t* out_actual,
                   int16_t* out_code) {
    return socket_with_event<SynchronousDatagramSocket>(zxio_synchronous_datagram_socket(io).client)
        .recvmsg(msg, flags, out_actual, out_code);
  };
  ops.sendmsg = [](zxio_t* io, const struct msghdr* msg, int flags, size_t* out_actual,
                   int16_t* out_code) {
    return socket_with_event<SynchronousDatagramSocket>(zxio_synchronous_datagram_socket(io).client)
        .sendmsg(msg, flags, out_actual, out_code);
  };
  ops.shutdown = [](zxio_t* io, zxio_shutdown_options_t options, int16_t* out_code) {
    return network_socket(zxio_synchronous_datagram_socket(io).client).shutdown(options, out_code);
  };
  return ops;
}();

zx_status_t zxio_synchronous_datagram_socket_init(
    zxio_storage_t* storage, zx::eventpair event,
    fidl::ClientEnd<fsocket::SynchronousDatagramSocket> client) {
  auto zs = new (storage) zxio_synchronous_datagram_socket_t{
      .io = storage->io,
      .event = std::move(event),
      .client = fidl::WireSyncClient(std::move(client)),
  };
  zxio_init(&zs->io, &zxio_synchronous_datagram_socket_ops);
  return ZX_OK;
}

static zxio_datagram_socket_t& zxio_datagram_socket(zxio_t* io) {
  return *reinterpret_cast<zxio_datagram_socket_t*>(io);
}

static constexpr zxio_ops_t zxio_datagram_socket_ops = []() {
  zxio_ops_t ops = zxio_default_socket_ops;
  ops.close = [](zxio_t* io) {
    zxio_datagram_socket_t& zs = zxio_datagram_socket(io);
    zx_status_t status = ZX_OK;
    if (zs.client.is_valid()) {
      status = base_socket(zs.client).CloseSocket();
    }
    zs.~zxio_datagram_socket();
    return status;
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    if (out_handle == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    *out_handle = zxio_datagram_socket(io).client.TakeClientEnd().TakeChannel().release();
    return ZX_OK;
  };
  ops.borrow = [](zxio_t* io, zx_handle_t* out_handle) {
    *out_handle = zxio_datagram_socket(io).client.client_end().borrow().channel()->get();
    return ZX_OK;
  };
  ops.clone = [](zxio_t* io, zx_handle_t* out_handle) {
    return base_socket(zxio_datagram_socket(io).client).CloneSocket(out_handle);
  };
  ops.wait_begin = [](zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                      zx_signals_t* out_zx_signals) {
    zxio_wait_begin(&zxio_datagram_socket(io).pipe.io, zxio_signals, out_handle, out_zx_signals);
  };
  ops.wait_end = [](zxio_t* io, zx_signals_t zx_signals, zxio_signals_t* out_zxio_signals) {
    zxio_wait_end(&zxio_datagram_socket(io).pipe.io, zx_signals, out_zxio_signals);
  };
  ops.readv = [](zxio_t* io, const zx_iovec_t* vector, size_t vector_count, zxio_flags_t flags,
                 size_t* out_actual) {
    return zxio_readv(&zxio_datagram_socket(io).pipe.io, vector, vector_count, flags, out_actual);
  };
  ops.writev = [](zxio_t* io, const zx_iovec_t* vector, size_t vector_count, zxio_flags_t flags,
                  size_t* out_actual) {
    return zxio_writev(&zxio_datagram_socket(io).pipe.io, vector, vector_count, flags, out_actual);
  };
  ops.shutdown = [](zxio_t* io, zxio_shutdown_options_t options, int16_t* out_code) {
    return network_socket(zxio_datagram_socket(io).client).shutdown(options, out_code);
  };
  ops.bind = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    return network_socket(zxio_datagram_socket(io).client).bind(addr, addrlen, out_code);
  };
  ops.connect = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    return network_socket(zxio_datagram_socket(io).client).connect(addr, addrlen, out_code);
  };
  ops.getsockname = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return network_socket(zxio_datagram_socket(io).client).getsockname(addr, addrlen, out_code);
  };
  ops.getpeername = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return network_socket(zxio_datagram_socket(io).client).getpeername(addr, addrlen, out_code);
  };
  ops.getsockopt = [](zxio_t* io, int level, int optname, void* optval, socklen_t* optlen,
                      int16_t* out_code) {
    SockOptResult result = network_socket(zxio_datagram_socket(io).client)
                               .getsockopt_fidl(level, optname, optval, optlen);
    *out_code = result.err;
    return result.status;
  };
  ops.setsockopt = [](zxio_t* io, int level, int optname, const void* optval, socklen_t optlen,
                      int16_t* out_code) {
    SockOptResult result = network_socket(zxio_datagram_socket(io).client)
                               .setsockopt_fidl(level, optname, optval, optlen);
    *out_code = result.err;
    return result.status;
  };
  return ops;
}();

zx_status_t zxio_datagram_socket_init(zxio_storage_t* storage, zx::socket socket,
                                      const zx_info_socket_t& info,
                                      const zxio_datagram_prelude_size_t& prelude_size,
                                      fidl::ClientEnd<fsocket::DatagramSocket> client) {
  auto zs = new (storage) zxio_datagram_socket_t{
      .io = {},
      .pipe = {},
      .prelude_size = prelude_size,
      .client = fidl::WireSyncClient(std::move(client)),
  };
  zxio_init(&zs->io, &zxio_datagram_socket_ops);
  return zxio_pipe_init(reinterpret_cast<zxio_storage_t*>(&zs->pipe), std::move(socket), info);
}

static zxio_stream_socket_t& zxio_stream_socket(zxio_t* io) {
  return *reinterpret_cast<zxio_stream_socket_t*>(io);
}

static constexpr zxio_ops_t zxio_stream_socket_ops = []() {
  zxio_ops_t ops = zxio_default_socket_ops;
  ops.close = [](zxio_t* io) {
    zxio_stream_socket_t& zs = zxio_stream_socket(io);
    zx_status_t status = ZX_OK;
    if (zs.client.is_valid()) {
      status = base_socket(zs.client).CloseSocket();
    }
    zs.~zxio_stream_socket_t();
    return status;
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    if (out_handle == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    *out_handle = zxio_stream_socket(io).client.TakeClientEnd().TakeChannel().release();
    return ZX_OK;
  };
  ops.borrow = [](zxio_t* io, zx_handle_t* out_handle) {
    *out_handle = zxio_stream_socket(io).client.client_end().borrow().channel()->get();
    return ZX_OK;
  };
  ops.clone = [](zxio_t* io, zx_handle_t* out_handle) {
    return base_socket(zxio_stream_socket(io).client).CloneSocket(out_handle);
  };
  ops.wait_begin = [](zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                      zx_signals_t* out_zx_signals) {
    zxio_wait_begin(&zxio_stream_socket(io).pipe.io, zxio_signals, out_handle, out_zx_signals);
  };
  ops.wait_end = [](zxio_t* io, zx_signals_t zx_signals, zxio_signals_t* out_zxio_signals) {
    zxio_wait_end(&zxio_stream_socket(io).pipe.io, zx_signals, out_zxio_signals);
  };
  ops.readv = [](zxio_t* io, const zx_iovec_t* vector, size_t vector_count, zxio_flags_t flags,
                 size_t* out_actual) {
    zx::socket& socket = zxio_stream_socket(io).pipe.socket;

    if (flags & ZXIO_PEEK) {
      uint32_t zx_flags = ZX_SOCKET_PEEK;
      flags &= ~ZXIO_PEEK;

      if (flags) {
        return ZX_ERR_NOT_SUPPORTED;
      }

      size_t total = 0;
      for (size_t i = 0; i < vector_count; ++i) {
        total += vector[i].capacity;
      }
      std::unique_ptr<uint8_t[]> buf(new uint8_t[total]);

      size_t actual;
      zx_status_t status = socket.read(zx_flags, buf.get(), total, &actual);
      if (status != ZX_OK) {
        return status;
      }

      uint8_t* data = buf.get();
      size_t remaining = actual;
      return zxio_do_vector(vector, vector_count, out_actual,
                            [&](void* buffer, size_t capacity, size_t* out_actual) {
                              size_t actual = std::min(capacity, remaining);
                              memcpy(buffer, data, actual);
                              data += actual;
                              remaining -= actual;
                              *out_actual = actual;
                              return ZX_OK;
                            });
    }

    if (flags) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    return zxio_do_vector(vector, vector_count, out_actual,
                          [&](void* buffer, size_t capacity, size_t* out_actual) {
                            return socket.read(0, buffer, capacity, out_actual);
                          });
  };
  ops.writev = [](zxio_t* io, const zx_iovec_t* vector, size_t vector_count, zxio_flags_t flags,
                  size_t* out_actual) {
    return zxio_writev(&zxio_stream_socket(io).pipe.io, vector, vector_count, flags, out_actual);
  };
  ops.get_read_buffer_available = [](zxio_t* io, size_t* out_available) {
    return zxio_get_read_buffer_available(&zxio_stream_socket(io).pipe.io, out_available);
  };
  ops.shutdown = [](zxio_t* io, zxio_shutdown_options_t options, int16_t* out_code) {
    return network_socket(zxio_stream_socket(io).client).shutdown(options, out_code);
  };
  ops.bind = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    return network_socket(zxio_stream_socket(io).client).bind(addr, addrlen, out_code);
  };
  ops.connect = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    zx_status_t status =
        network_socket(zxio_stream_socket(io).client).connect(addr, addrlen, out_code);
    if (status == ZX_OK) {
      std::lock_guard lock(zxio_stream_socket(io).state_lock);
      switch (*out_code) {
        case 0:
          zxio_stream_socket(io).state = zxio_stream_socket_state_t::CONNECTED;
          break;
        case EINPROGRESS:
          zxio_stream_socket(io).state = zxio_stream_socket_state_t::CONNECTING;
          break;
      }
    }
    return status;
  };
  ops.listen = [](zxio_t* io, int backlog, int16_t* out_code) {
    auto response =
        zxio_stream_socket(io).client->Listen(safemath::saturated_cast<int16_t>(backlog));
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
      return ZX_OK;
    }
    {
      std::lock_guard lock(zxio_stream_socket(io).state_lock);
      zxio_stream_socket(io).state = zxio_stream_socket_state_t::LISTENING;
    }
    *out_code = 0;
    return ZX_OK;
  };
  ops.accept = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                  zxio_storage_t* out_storage, int16_t* out_code) {
    bool want_addr = addr != nullptr && addrlen != nullptr;
    auto response = zxio_stream_socket(io).client->Accept(want_addr);
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    const auto& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
      return ZX_OK;
    }
    *out_code = 0;
    auto const& out = result.value()->addr;
    // Result address has invalid tag when it's not provided by the server (when want_addr
    // is false).
    // TODO(https://fxbug.dev/58503): Use better representation of nullable union when available.
    if (want_addr && !out.has_invalid_tag()) {
      *addrlen = static_cast<socklen_t>(zxio_fidl_to_sockaddr(out, addr, *addrlen));
    }

    fidl::ClientEnd<fsocket::StreamSocket>& control = result.value()->s;
    fidl::WireResult describe_result = fidl::WireCall(control)->Describe2();
    if (!describe_result.ok()) {
      return describe_result.status();
    }
    fidl::WireResponse describe_response = describe_result.value();
    if (!describe_response.has_socket()) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    zx::socket& socket = describe_response.socket();
    zx_info_socket_t info;
    if (zx_status_t status = socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
        status != ZX_OK) {
      return status;
    }
    if (zx_status_t status = zxio_stream_socket_init(out_storage, std::move(socket), info,
                                                     /*is_connected=*/true, std::move(control));
        status != ZX_OK) {
      return status;
    }
    return ZX_OK;
  };
  ops.getsockname = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return network_socket(zxio_stream_socket(io).client).getsockname(addr, addrlen, out_code);
  };
  ops.getpeername = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return network_socket(zxio_stream_socket(io).client).getpeername(addr, addrlen, out_code);
  };
  ops.getsockopt = [](zxio_t* io, int level, int optname, void* optval, socklen_t* optlen,
                      int16_t* out_code) {
    SockOptResult result = network_socket(zxio_stream_socket(io).client)
                               .getsockopt_fidl(level, optname, optval, optlen);
    *out_code = result.err;
    return result.status;
  };
  ops.setsockopt = [](zxio_t* io, int level, int optname, const void* optval, socklen_t optlen,
                      int16_t* out_code) {
    SockOptResult result = network_socket(zxio_stream_socket(io).client)
                               .setsockopt_fidl(level, optname, optval, optlen);
    *out_code = result.err;
    return result.status;
  };
  return ops;
}();

zx_status_t zxio_stream_socket_init(zxio_storage_t* storage, zx::socket socket,
                                    const zx_info_socket_t& info, const bool is_connected,
                                    fidl::ClientEnd<fsocket::StreamSocket> client) {
  zxio_stream_socket_state_t state = is_connected ? zxio_stream_socket_state_t::CONNECTED
                                                  : zxio_stream_socket_state_t::UNCONNECTED;
  auto zs = new (storage) zxio_stream_socket_t{
      .io = {},
      .pipe = {},
      .state_lock = {},
      .state = state,
      .client = fidl::WireSyncClient(std::move(client)),
  };
  zxio_init(&zs->io, &zxio_stream_socket_ops);
  return zxio_pipe_init(reinterpret_cast<zxio_storage_t*>(&zs->pipe), std::move(socket), info);
}

static zxio_raw_socket_t& zxio_raw_socket(zxio_t* io) {
  return *reinterpret_cast<zxio_raw_socket_t*>(io);
}

static constexpr zxio_ops_t zxio_raw_socket_ops = []() {
  zxio_ops_t ops = zxio_default_socket_ops;
  ops.close = [](zxio_t* io) {
    zxio_raw_socket_t& zs = zxio_raw_socket(io);
    zx_status_t status = ZX_OK;
    if (zs.client.is_valid()) {
      status = base_socket(zs.client).CloseSocket();
    }
    zs.~zxio_raw_socket_t();
    return status;
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    if (out_handle == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    *out_handle = zxio_raw_socket(io).client.TakeClientEnd().TakeChannel().release();
    return ZX_OK;
  };
  ops.borrow = [](zxio_t* io, zx_handle_t* out_handle) {
    *out_handle = zxio_raw_socket(io).client.client_end().borrow().channel()->get();
    return ZX_OK;
  };
  ops.clone = [](zxio_t* io, zx_handle_t* out_handle) {
    zxio_raw_socket_t& zs = zxio_raw_socket(io);
    zx_status_t status = base_socket(zs.client).CloneSocket(out_handle);
    return status;
  };
  ops.bind = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    return network_socket(zxio_raw_socket(io).client).bind(addr, addrlen, out_code);
  };
  ops.connect = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    return network_socket(zxio_raw_socket(io).client).connect(addr, addrlen, out_code);
  };
  ops.getsockname = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return network_socket(zxio_raw_socket(io).client).getsockname(addr, addrlen, out_code);
  };
  ops.getpeername = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return network_socket(zxio_raw_socket(io).client).getpeername(addr, addrlen, out_code);
  };
  ops.getsockopt = [](zxio_t* io, int level, int optname, void* optval, socklen_t* optlen,
                      int16_t* out_code) {
    SockOptResult result = [&]() {
      GetSockOptProcessor proc(optval, optlen);
      switch (level) {
        case SOL_ICMPV6:
          switch (optname) {
            case ICMP6_FILTER:
              return proc.Process(zxio_raw_socket(io).client->GetIcmpv6Filter(),
                                  [](const auto& response) { return response.filter; });
          }
          break;
        case SOL_IPV6:
          switch (optname) {
            case IPV6_CHECKSUM:
              return proc.Process(
                  zxio_raw_socket(io).client->GetIpv6Checksum(), [](const auto& response) {
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
              return proc.Process(zxio_raw_socket(io).client->GetIpHeaderIncluded(),
                                  [](const auto& response) { return response.value; });
          }
          break;
      }
      return network_socket(zxio_raw_socket(io).client)
          .getsockopt_fidl(level, optname, optval, optlen);
    }();
    *out_code = result.err;
    return result.status;
  };
  ops.getsockopt = [](zxio_t* io, int level, int optname, void* optval, socklen_t* optlen,
                      int16_t* out_code) {
    SockOptResult result = [&]() {
      GetSockOptProcessor proc(optval, optlen);
      switch (level) {
        case SOL_ICMPV6:
          switch (optname) {
            case ICMP6_FILTER:
              return proc.Process(zxio_raw_socket(io).client->GetIcmpv6Filter(),
                                  [](const auto& response) { return response.filter; });
          }
          break;
        case SOL_IPV6:
          switch (optname) {
            case IPV6_CHECKSUM:
              return proc.Process(
                  zxio_raw_socket(io).client->GetIpv6Checksum(), [](const auto& response) {
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
              return proc.Process(zxio_raw_socket(io).client->GetIpHeaderIncluded(),
                                  [](const auto& response) { return response.value; });
          }
          break;
      }
      return network_socket(zxio_raw_socket(io).client)
          .getsockopt_fidl(level, optname, optval, optlen);
    }();
    *out_code = result.err;
    return result.status;
  };
  ops.setsockopt = [](zxio_t* io, int level, int optname, const void* optval, socklen_t optlen,
                      int16_t* out_code) {
    SockOptResult result = [&]() {
      SetSockOptProcessor proc(optval, optlen);

      switch (level) {
        case SOL_ICMPV6:
          switch (optname) {
            case ICMP6_FILTER:
              return proc.Process<frawsocket::wire::Icmpv6Filter>(
                  [io](frawsocket::wire::Icmpv6Filter value) {
                    return zxio_raw_socket(io).client->SetIcmpv6Filter(value);
                  });
          }
          break;
        case SOL_IPV6:
          switch (optname) {
            case IPV6_CHECKSUM:
              return proc.Process<int32_t>([io](int32_t value) {
                frawsocket::wire::Ipv6ChecksumConfiguration config;

                if (value < 0) {
                  config = frawsocket::wire::Ipv6ChecksumConfiguration::WithDisabled(
                      frawsocket::wire::Empty{});
                } else {
                  config = frawsocket::wire::Ipv6ChecksumConfiguration::WithOffset(value);
                }

                return zxio_raw_socket(io).client->SetIpv6Checksum(config);
              });
          }
          break;
        case SOL_IP:
          switch (optname) {
            case IP_HDRINCL:
              return proc.Process<bool>([io](bool value) {
                return zxio_raw_socket(io).client->SetIpHeaderIncluded(value);
              });
          }
          break;
      }
      return network_socket(zxio_raw_socket(io).client)
          .setsockopt_fidl(level, optname, optval, optlen);
    }();
    *out_code = result.err;
    return result.status;
  };
  ops.recvmsg = [](zxio_t* io, struct msghdr* msg, int flags, size_t* out_actual,
                   int16_t* out_code) {
    return socket_with_event<RawSocket>(zxio_raw_socket(io).client)
        .recvmsg(msg, flags, out_actual, out_code);
  };
  ops.sendmsg = [](zxio_t* io, const struct msghdr* msg, int flags, size_t* out_actual,
                   int16_t* out_code) {
    return socket_with_event<RawSocket>(zxio_raw_socket(io).client)
        .sendmsg(msg, flags, out_actual, out_code);
  };
  ops.shutdown = [](zxio_t* io, zxio_shutdown_options_t options, int16_t* out_code) {
    return network_socket(zxio_raw_socket(io).client).shutdown((options), out_code);
  };
  return ops;
}();

zx_status_t zxio_raw_socket_init(zxio_storage_t* storage, zx::eventpair event,
                                 fidl::ClientEnd<frawsocket::Socket> client) {
  auto zs = new (storage) zxio_raw_socket_t{
      .io = storage->io,
      .event = std::move(event),
      .client = fidl::WireSyncClient(std::move(client)),
  };
  zxio_init(&zs->io, &zxio_raw_socket_ops);
  return ZX_OK;
}

static zxio_packet_socket_t& zxio_packet_socket(zxio_t* io) {
  return *reinterpret_cast<zxio_packet_socket_t*>(io);
}

static constexpr zxio_ops_t zxio_packet_socket_ops = []() {
  zxio_ops_t ops = zxio_default_socket_ops;
  ops.close = [](zxio_t* io) {
    zxio_packet_socket_t& zs = zxio_packet_socket(io);
    zx_status_t status = ZX_OK;
    if (zs.client.is_valid()) {
      status = base_socket(zs.client).CloseSocket();
    }
    zs.~zxio_packet_socket_t();
    return status;
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    if (out_handle == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    *out_handle = zxio_packet_socket(io).client.TakeClientEnd().TakeChannel().release();
    return ZX_OK;
  };
  ops.borrow = [](zxio_t* io, zx_handle_t* out_handle) {
    *out_handle = zxio_packet_socket(io).client.client_end().borrow().channel()->get();
    return ZX_OK;
  };
  ops.clone = [](zxio_t* io, zx_handle_t* out_handle) {
    zxio_packet_socket_t& zs = zxio_packet_socket(io);
    zx_status_t status = base_socket(zs.client).CloneSocket(out_handle);
    return status;
  };
  ops.bind = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    if (addr == nullptr || addrlen < sizeof(sockaddr_ll)) {
      return ZX_ERR_INVALID_ARGS;
    }

    const sockaddr_ll& sll = *reinterpret_cast<const sockaddr_ll*>(addr);

    fpacketsocket::wire::ProtocolAssociation proto_assoc;
    uint16_t protocol = ntohs(sll.sll_protocol);
    switch (protocol) {
      case 0:
        // protocol association is optional.
        break;
      case ETH_P_ALL:
        proto_assoc =
            fpacketsocket::wire::ProtocolAssociation::WithAll(fpacketsocket::wire::Empty());
        break;
      default:
        proto_assoc = fpacketsocket::wire::ProtocolAssociation::WithSpecified(protocol);
        break;
    }

    fpacketsocket::wire::BoundInterfaceId interface_id;
    uint64_t ifindex = sll.sll_ifindex;
    if (ifindex == 0) {
      interface_id = fpacketsocket::wire::BoundInterfaceId::WithAll(fpacketsocket::wire::Empty());
    } else {
      interface_id = fpacketsocket::wire::BoundInterfaceId::WithSpecified(
          fidl::ObjectView<uint64_t>::FromExternal(&ifindex));
    }

    const fidl::WireResult response =
        zxio_packet_socket(io).client->Bind(proto_assoc, interface_id);
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    const auto& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
      return ZX_OK;
    }
    *out_code = 0;
    return ZX_OK;
  };
  ops.getsockname = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    if (addrlen == nullptr || (*addrlen != 0 && addr == nullptr)) {
      *out_code = EFAULT;
      return ZX_OK;
    }

    const fidl::WireResult response = zxio_packet_socket(io).client->GetInfo();
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    const auto& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
      return ZX_OK;
    }
    *out_code = 0;

    const fpacketsocket::wire::SocketGetInfoResponse& info = *result.value();
    sockaddr_ll sll = {
        .sll_family = AF_PACKET,
        .sll_protocol = htons(fidl_protoassoc_to_protocol(info.protocol)),
    };

    switch (info.bound_interface.Which()) {
      case fpacketsocket::wire::BoundInterface::Tag::kAll:
        sll.sll_ifindex = 0;
        sll.sll_halen = 0;
        sll.sll_hatype = 0;
        break;
      case fpacketsocket::wire::BoundInterface::Tag::kSpecified: {
        const fpacketsocket::wire::InterfaceProperties& props = info.bound_interface.specified();
        sll.sll_ifindex = static_cast<int>(props.id);
        sll.sll_hatype = fidl_hwtype_to_arphrd(props.type);
        populate_from_fidl_hwaddr(props.addr, sll);
      } break;
    }

    socklen_t used_bytes = offsetof(sockaddr_ll, sll_addr) + sll.sll_halen;
    memcpy(addr, &sll, std::min(used_bytes, *addrlen));
    *addrlen = used_bytes;
    return ZX_OK;
  };
  ops.getsockopt = [](zxio_t* io, int level, int optname, void* optval, socklen_t* optlen,
                      int16_t* out_code) {
    SockOptResult result = [&]() {
      switch (level) {
        case SOL_SOCKET:
          return base_socket(zxio_packet_socket(io).client)
              .get_solsocket_sockopt_fidl(optname, optval, optlen);
        default:
          return SockOptResult::Errno(EPROTONOSUPPORT);
      }
    }();
    *out_code = result.err;
    return result.status;
  };
  ops.setsockopt = [](zxio_t* io, int level, int optname, const void* optval, socklen_t optlen,
                      int16_t* out_code) {
    SockOptResult result = [&]() {
      switch (level) {
        case SOL_SOCKET:
          return base_socket(zxio_packet_socket(io).client)
              .set_solsocket_sockopt_fidl(optname, optval, optlen);
        default:
          return SockOptResult::Errno(EPROTONOSUPPORT);
      }
    }();
    *out_code = result.err;
    return result.status;
  };
  ops.recvmsg = [](zxio_t* io, struct msghdr* msg, int flags, size_t* out_actual,
                   int16_t* out_code) {
    return socket_with_event<PacketSocket>(zxio_packet_socket(io).client)
        .recvmsg(msg, flags, out_actual, out_code);
  };
  ops.sendmsg = [](zxio_t* io, const struct msghdr* msg, int flags, size_t* out_actual,
                   int16_t* out_code) {
    return socket_with_event<PacketSocket>(zxio_packet_socket(io).client)
        .sendmsg(msg, flags, out_actual, out_code);
  };
  return ops;
}();

zx_status_t zxio_packet_socket_init(zxio_storage_t* storage, zx::eventpair event,
                                    fidl::ClientEnd<fpacketsocket::Socket> client) {
  auto zs = new (storage) zxio_packet_socket_t{
      .io = storage->io,
      .event = std::move(event),
      .client = fidl::WireSyncClient(std::move(client)),
  };
  zxio_init(&zs->io, &zxio_packet_socket_ops);
  return ZX_OK;
}
