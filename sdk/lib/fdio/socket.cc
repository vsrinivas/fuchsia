// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fdio.h>
#include <lib/zx/socket.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/cpp/vector.h>
#include <lib/zxio/null.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>

#include <vector>

#include <safemath/safe_conversions.h>

#include "fdio_unistd.h"
#include "private-socket.h"
#include "zxio.h"

namespace fio = fuchsia_io;
namespace fsocket = fuchsia_posix_socket;
namespace frawsocket = fuchsia_posix_socket_raw;
namespace fnet = fuchsia_net;

namespace {

// TODO(https://fxbug.dev/78129): Remove after ABI transition.
bool use_legacy_shutdown2_fidl() {
  static std::once_flag once;
  static bool legacy;

  std::call_once(once, [&]() {
    legacy = []() {
      constexpr char kLegacyShutdown2Fidl[] = "LEGACY_SHUTDOWN2_FIDL";
      const char* const legacy_env = getenv(kLegacyShutdown2Fidl);
      return legacy_env && strcmp(legacy_env, "1") == 0;
    }();
  });
  return legacy;
}

// A helper structure to keep a socket address and the variants allocations in stack.
struct SocketAddress {
  fnet::wire::SocketAddress address;
  union U {
    fnet::wire::Ipv4SocketAddress ipv4;
    fnet::wire::Ipv6SocketAddress ipv6;

    U() { memset(this, 0x00, sizeof(U)); }
  } storage;

  zx_status_t LoadSockAddr(const struct sockaddr* addr, size_t addr_len) {
    // Address length larger than sockaddr_storage causes an error for API compatibility only.
    if (addr == nullptr || addr_len > sizeof(struct sockaddr_storage)) {
      return ZX_ERR_INVALID_ARGS;
    }
    switch (addr->sa_family) {
      case AF_INET: {
        if (addr_len < sizeof(struct sockaddr_in)) {
          return ZX_ERR_INVALID_ARGS;
        }
        const auto& s = *reinterpret_cast<const struct sockaddr_in*>(addr);
        address.set_ipv4(
            fidl::ObjectView<fnet::wire::Ipv4SocketAddress>::FromExternal(&storage.ipv4));
        std::copy_n(reinterpret_cast<const uint8_t*>(&s.sin_addr.s_addr),
                    decltype(storage.ipv4.address.addr)::size(), storage.ipv4.address.addr.begin());
        storage.ipv4.port = ntohs(s.sin_port);
        return ZX_OK;
      }
      case AF_INET6: {
        if (addr_len < sizeof(struct sockaddr_in6)) {
          return ZX_ERR_INVALID_ARGS;
        }
        const auto& s = *reinterpret_cast<const struct sockaddr_in6*>(addr);
        address.set_ipv6(
            fidl::ObjectView<fnet::wire::Ipv6SocketAddress>::FromExternal(&storage.ipv6));
        std::copy(std::begin(s.sin6_addr.s6_addr), std::end(s.sin6_addr.s6_addr),
                  storage.ipv6.address.addr.begin());
        storage.ipv6.port = ntohs(s.sin6_port);
        storage.ipv6.zone_index = s.sin6_scope_id;
        return ZX_OK;
      }
      default:
        return ZX_ERR_INVALID_ARGS;
    }
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

socklen_t fidl_to_sockaddr(const fnet::wire::SocketAddress& fidl, struct sockaddr* addr,
                           socklen_t addr_len) {
  switch (fidl.which()) {
    case fnet::wire::SocketAddress::Tag::kIpv4: {
      struct sockaddr_in tmp;
      auto* s = reinterpret_cast<struct sockaddr_in*>(addr);
      if (addr_len < sizeof(tmp)) {
        s = &tmp;
      }
      memset(s, 0x00, addr_len);
      const auto& ipv4 = fidl.ipv4();
      s->sin_family = AF_INET;
      s->sin_port = htons(ipv4.port);
      std::copy(ipv4.address.addr.begin(), ipv4.address.addr.end(),
                reinterpret_cast<uint8_t*>(&s->sin_addr));
      // Copy truncated address.
      if (s == &tmp) {
        memcpy(addr, &tmp, addr_len);
      }
      return sizeof(tmp);
    }
    case fnet::wire::SocketAddress::Tag::kIpv6: {
      struct sockaddr_in6 tmp;
      auto* s = reinterpret_cast<struct sockaddr_in6*>(addr);
      if (addr_len < sizeof(tmp)) {
        s = &tmp;
      }
      memset(s, 0x00, addr_len);
      const auto& ipv6 = fidl.ipv6();
      s->sin6_family = AF_INET6;
      s->sin6_port = htons(ipv6.port);
      s->sin6_scope_id = static_cast<uint32_t>(ipv6.zone_index);
      std::copy(ipv6.address.addr.begin(), ipv6.address.addr.end(),
                s->sin6_addr.__in6_union.__s6_addr);
      // Copy truncated address.
      if (s == &tmp) {
        memcpy(addr, &tmp, addr_len);
      }
      return sizeof(tmp);
    }
  }
}

// https://github.com/torvalds/linux/blob/f2850dd5ee015bd7b77043f731632888887689c7/include/net/tcp.h#L1012
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
    const auto& result = response.value().result;
    if (result.is_err()) {
      return SockOptResult::Errno(static_cast<int16_t>(result.err()));
    }
    return SockOptResult::Ok();
  }
};

class GetSockOptProcessor {
 public:
  GetSockOptProcessor(void* optval, socklen_t* optlen) : optval_(optval), optlen_(optlen) {}

  template <typename T, typename F>
  SockOptResult Process(T response, F getter) {
    if (response.status() != ZX_OK) {
      return SockOptResult::Zx(response.status());
    }
    auto& value = response.value();
    if (value.result.is_err()) {
      return SockOptResult::Errno(static_cast<int16_t>(value.result.err()));
    }
    return StoreOption(getter(value.result.response()));
  }

  template <typename T>
  SockOptResult StoreOption(const T& value) {
    static_assert(sizeof(T) != sizeof(T), "function must be specialized");
  };

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
  switch (value.which()) {
    case fsocket::wire::OptionalUint8::Tag::kValue:
      return StoreOption(static_cast<int32_t>(value.value()));
    case fsocket::wire::OptionalUint8::Tag::kUnset:
      return StoreOption(-1);
  }
}

template <>
SockOptResult GetSockOptProcessor::StoreOption(const fsocket::wire::OptionalUint32& value) {
  switch (value.which()) {
    case fsocket::wire::OptionalUint32::Tag::kValue:
      ZX_ASSERT(value.value() < std::numeric_limits<int32_t>::max());
      return StoreOption(static_cast<int32_t>(value.value()));
    case fsocket::wire::OptionalUint32::Tag::kUnset:
      return StoreOption(-1);
  }
}

template <>
SockOptResult GetSockOptProcessor::StoreOption(const fnet::wire::Ipv4Address& value) {
  static_assert(sizeof(struct in_addr) == decltype(value.addr)::size());
  return StoreRaw(value.addr.data(), decltype(value.addr)::size());
}

template <>
SockOptResult GetSockOptProcessor::StoreOption(const fsocket::wire::TcpInfo& value) {
  tcp_info info;
  // Explicitly initialize unsupported fields to a garbage value. It would probably be quieter to
  // zero-initialize, but that can mask bugs in the interpretation of fields for which zero is a
  // valid value.
  //
  // Note that "unsupported" includes fields not defined in FIDL *and* fields not populated by the
  // server.
  memset(&info, 0xff, sizeof(info));

  if (value.has_state()) {
    info.tcpi_state = [](fsocket::wire::TcpState state) {
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
    info.tcpi_ca_state = [](fsocket::wire::TcpCongestionControlState ca_state) {
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

  return StoreRaw(&info, std::min(*optlen_, socklen_t(sizeof(info))));
}

// Used for various options that allow the caller to supply larger buffers than needed.
struct PartialCopy {
  int32_t value;
  // Appears to be true for IP_* and false for IPV6_*.
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
  int16_t Get(T* out) {
    if (optlen_ < sizeof(T)) {
      return EINVAL;
    }
    memcpy(out, optval_, sizeof(T));
    return 0;
  }

  template <typename T, typename F>
  SockOptResult Process(F f) {
    T v;
    int16_t result = Get(&v);
    if (result) {
      return SockOptResult::Errno(result);
    }
    return SockOptResult::FromFidlResponse(f(std::move(v)));
  }

 private:
  const void* const optval_;
  socklen_t const optlen_;
  fsocket::wire::Empty empty_;
};

template <>
int16_t SetSockOptProcessor::Get(fidl::StringView* out) {
  const char* optval = static_cast<const char*>(optval_);
  *out = fidl::StringView::FromExternal(optval, strnlen(optval, optlen_));
  return 0;
}

template <>
int16_t SetSockOptProcessor::Get(bool* out) {
  int32_t i;
  int16_t r = Get(&i);
  *out = i != 0;
  return r;
}

template <>
int16_t SetSockOptProcessor::Get(uint32_t* out) {
  auto* alt = reinterpret_cast<int32_t*>(out);
  int16_t r = Get(alt);
  if (r) {
    return r;
  }
  if (*alt < 0) {
    return EINVAL;
  }
  return 0;
}

template <typename T, typename V>
struct OptionalStorage {
  T opt;
  union U {
    fsocket::wire::Empty empty;
    V value;

    U() { memset(this, 0x00, sizeof(U)); }
  } v;

  void set_unset() {
    opt.set_unset(fidl::ObjectView<fsocket::wire::Empty>::FromExternal(&v.empty));
  }

  void set_value(V value) {
    v.value = value;
    opt.set_value(fidl::ObjectView<V>::FromExternal(&v.value));
  }
};

using OptionalUint8 = OptionalStorage<fsocket::wire::OptionalUint8, uint8_t>;
using OptionalUint32 = OptionalStorage<fsocket::wire::OptionalUint32, uint32_t>;

template <>
int16_t SetSockOptProcessor::Get(OptionalUint8* out) {
  int32_t i;
  if (int16_t r = Get(&i); r) {
    return r;
  }
  if (i < -1 || i > std::numeric_limits<uint8_t>::max()) {
    return EINVAL;
  }
  if (i == -1) {
    out->set_unset();
  } else {
    out->set_value(static_cast<uint8_t>(i));
  }
  return 0;
}

// Like OptionalUint8, but permits truncation to a single byte.
struct OptionalUint8CharAllowed {
  OptionalUint8 inner;
};

template <>
int16_t SetSockOptProcessor::Get(OptionalUint8CharAllowed* out) {
  if (optlen_ == sizeof(uint8_t)) {
    out->inner.set_value(out->inner.v.value);
    memcpy(&out->inner.v.value, optval_, sizeof(uint8_t));
    return 0;
  }
  return Get(&out->inner);
}

template <>
int16_t SetSockOptProcessor::Get(fsocket::wire::IpMulticastMembership* out) {
  union {
    struct ip_mreqn reqn;
    struct ip_mreq req;
  } r;
  struct in_addr* local;
  struct in_addr* mcast;
  if (optlen_ < sizeof(struct ip_mreqn)) {
    if (Get(&r.req) != 0) {
      return EINVAL;
    }
    out->iface = 0;
    local = &r.req.imr_interface;
    mcast = &r.req.imr_multiaddr;
  } else {
    if (Get(&r.reqn) != 0) {
      return EINVAL;
    }
    out->iface = r.reqn.imr_ifindex;
    local = &r.reqn.imr_address;
    mcast = &r.reqn.imr_multiaddr;
  }
  std::copy_n(reinterpret_cast<const uint8_t*>(local), out->local_addr.addr.size(),
              out->local_addr.addr.begin());
  std::copy_n(reinterpret_cast<const uint8_t*>(mcast), out->mcast_addr.addr.size(),
              out->mcast_addr.addr.begin());
  return 0;
}

template <>
int16_t SetSockOptProcessor::Get(fsocket::wire::Ipv6MulticastMembership* out) {
  struct ipv6_mreq req;
  if (Get(&req) != 0) {
    return EINVAL;
  }
  out->iface = req.ipv6mr_interface;
  auto const& mcast = req.ipv6mr_multiaddr.s6_addr;
  std::copy(std::begin(mcast), std::end(mcast), out->mcast_addr.addr.begin());
  return 0;
}

template <>
int16_t SetSockOptProcessor::Get(fsocket::wire::TcpCongestionControl* out) {
  if (strncmp(static_cast<const char*>(optval_), kCcCubic, optlen_) == 0) {
    *out = fsocket::wire::TcpCongestionControl::kCubic;
    return 0;
  }
  if (strncmp(static_cast<const char*>(optval_), kCcReno, optlen_) == 0) {
    *out = fsocket::wire::TcpCongestionControl::kReno;
    return 0;
  }
  return ENOENT;
}

struct IntOrChar {
  int32_t value;
};

template <>
int16_t SetSockOptProcessor::Get(IntOrChar* out) {
  if (Get(&out->value) == 0) {
    return 0;
  }
  if (optlen_ == 0) {
    return EINVAL;
  }
  out->value = static_cast<const uint8_t*>(optval_)[0];
  return 0;
}

template <typename T,
          typename =
              std::enable_if_t<std::is_same_v<T, fidl::WireSyncClient<fsocket::DatagramSocket>> ||
                               std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>> ||
                               std::is_same_v<T, fidl::WireSyncClient<frawsocket::Socket>>>>
struct BaseSocket {
  static_assert(std::is_same_v<T, fidl::WireSyncClient<fsocket::DatagramSocket>> ||
                std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>> ||
                std::is_same_v<T, fidl::WireSyncClient<frawsocket::Socket>>);

 public:
  explicit BaseSocket(T& client) : client_(client) {}

  T& client() { return client_; }

  zx_status_t bind(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    SocketAddress fidl_addr;
    zx_status_t status = fidl_addr.LoadSockAddr(addr, addrlen);
    if (status != ZX_OK) {
      return status;
    }

    auto response = client().Bind(fidl_addr.address);
    status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.Unwrap()->result;
    if (result.is_err()) {
      *out_code = static_cast<int16_t>(result.err());
      return ZX_OK;
    }
    *out_code = 0;
    return ZX_OK;
  }

  zx_status_t connect(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    // If address is AF_UNSPEC we should call disconnect.
    if (addr->sa_family == AF_UNSPEC) {
      auto response = client().Disconnect();
      zx_status_t status = response.status();
      if (status != ZX_OK) {
        return status;
      }
      const auto& result = response.Unwrap()->result;
      if (result.is_err()) {
        *out_code = static_cast<int16_t>(result.err());
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

    auto response = client().Connect(fidl_addr.address);
    status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.Unwrap()->result;
    if (result.is_err()) {
      *out_code = static_cast<int16_t>(result.err());
      return ZX_OK;
    }
    *out_code = 0;
    return ZX_OK;
  }

  template <typename R>
  zx_status_t getname(R response, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.Unwrap()->result;
    if (result.is_err()) {
      *out_code = static_cast<int16_t>(result.err());
      return ZX_OK;
    }
    if (addrlen == nullptr || (*addrlen != 0 && addr == nullptr)) {
      *out_code = EFAULT;
      return ZX_OK;
    }
    *out_code = 0;
    auto const& out = result.response().addr;
    *addrlen = fidl_to_sockaddr(out, addr, *addrlen);
    return ZX_OK;
  }

  zx_status_t getsockname(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return getname(client().GetSockName(), addr, addrlen, out_code);
  }

  zx_status_t getpeername(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return getname(client().GetPeerName(), addr, addrlen, out_code);
  }

  SockOptResult getsockopt_fidl(int level, int optname, void* optval, socklen_t* optlen) {
    GetSockOptProcessor proc(optval, optlen);
    switch (level) {
      case SOL_SOCKET:
        switch (optname) {
          case SO_TYPE:
            if constexpr (std::is_same_v<T, fidl::WireSyncClient<fsocket::DatagramSocket>>) {
              return proc.StoreOption<int32_t>(SOCK_DGRAM);
            }
            if constexpr (std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>>) {
              return proc.StoreOption<int32_t>(SOCK_STREAM);
            }
            if constexpr (std::is_same_v<T, fidl::WireSyncClient<frawsocket::Socket>>) {
              return proc.StoreOption<int32_t>(SOCK_RAW);
            }
          case SO_DOMAIN:
            return proc.Process(client().GetInfo(),
                                [](const auto& response) { return response.domain; });
          case SO_TIMESTAMP:
            return proc.Process(client().GetTimestamp(),
                                [](const auto& response) { return response.value; });
          case SO_PROTOCOL:
            if constexpr (std::is_same_v<T, fidl::WireSyncClient<fsocket::DatagramSocket>>) {
              return proc.Process(client().GetInfo(), [](const auto& response) {
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
              return proc.Process(client().GetInfo(), [](const auto& response) {
                switch (response.proto) {
                  case fsocket::wire::StreamSocketProtocol::kTcp:
                    return IPPROTO_TCP;
                }
              });
            }
            if constexpr (std::is_same_v<T, fidl::WireSyncClient<frawsocket::Socket>>) {
              return proc.Process(client().GetInfo(), [](const auto& response) {
                switch (response.proto.which()) {
                  case frawsocket::wire::ProtocolAssociation::Tag::kUnassociated:
                    return IPPROTO_RAW;
                  case frawsocket::wire::ProtocolAssociation::Tag::kAssociated:
                    return static_cast<int>(response.proto.associated());
                }
              });
            }
          case SO_ERROR: {
            auto response = client().GetError();
            if (response.status() != ZX_OK) {
              return SockOptResult::Zx(response.status());
            }
            int32_t error_code = 0;
            auto& value = response.value();
            if (value.result.is_err()) {
              error_code = static_cast<int32_t>(value.result.err());
            }
            return proc.StoreOption(error_code);
          }
          case SO_SNDBUF:
            return proc.Process(client().GetSendBuffer(), [](const auto& response) {
              return static_cast<uint32_t>(response.value_bytes);
            });
          case SO_RCVBUF:
            return proc.Process(client().GetReceiveBuffer(), [](const auto& response) {
              return static_cast<uint32_t>(response.value_bytes);
            });
          case SO_REUSEADDR:
            return proc.Process(client().GetReuseAddress(),
                                [](const auto& response) { return response.value; });
          case SO_REUSEPORT:
            return proc.Process(client().GetReusePort(),
                                [](const auto& response) { return response.value; });
          case SO_BINDTODEVICE:
            return proc.Process(
                client().GetBindToDevice(),
                [](auto& response) -> const fidl::StringView& { return response.value; });
          case SO_BROADCAST:
            return proc.Process(client().GetBroadcast(),
                                [](const auto& response) { return response.value; });
          case SO_KEEPALIVE:
            return proc.Process(client().GetKeepAlive(),
                                [](const auto& response) { return response.value; });
          case SO_LINGER:
            return proc.Process(client().GetLinger(), [](const auto& response) {
              struct linger l;
              l.l_onoff = response.linger;
              // NB: l_linger is typed as int but interpreted as unsigned by
              // linux.
              l.l_linger = static_cast<int>(response.length_secs);
              return l;
            });
          case SO_ACCEPTCONN:
            return proc.Process(client().GetAcceptConn(),
                                [](const auto& response) { return response.value; });
          case SO_OOBINLINE:
            return proc.Process(client().GetOutOfBandInline(),
                                [](const auto& response) { return response.value; });
          case SO_NO_CHECK:
            return proc.Process(client().GetNoCheck(),
                                [](const auto& response) { return response.value; });
          case SO_SNDTIMEO:
          case SO_RCVTIMEO:
          case SO_PEERCRED:
            return SockOptResult::Errno(EOPNOTSUPP);
          default:
            return SockOptResult::Errno(ENOPROTOOPT);
        }
      case SOL_IP:
        switch (optname) {
          case IP_TTL:
            return proc.Process(client().GetIpTtl(), [](const auto& response) {
              return static_cast<int32_t>(response.value);
            });
          case IP_MULTICAST_TTL:
            return proc.Process(client().GetIpMulticastTtl(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = true,
              };
            });
          case IP_MULTICAST_IF:
            return proc.Process(client().GetIpMulticastInterface(),
                                [](const auto& response) { return response.value; });
          case IP_MULTICAST_LOOP:
            return proc.Process(client().GetIpMulticastLoopback(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = true,
              };
            });
          case IP_TOS:
            return proc.Process(client().GetIpTypeOfService(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = true,
              };
            });
          case IP_RECVTOS:
            return proc.Process(client().GetIpReceiveTypeOfService(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = true,
              };
            });
          case IP_PKTINFO:
            return proc.Process(client().GetIpPacketInfo(),
                                [](const auto& response) { return response.value; });
          default:
            return SockOptResult::Errno(ENOPROTOOPT);
        }
      case SOL_IPV6:
        switch (optname) {
          case IPV6_V6ONLY:
            return proc.Process(client().GetIpv6Only(),
                                [](const auto& response) { return response.value; });
          case IPV6_TCLASS:
            return proc.Process(client().GetIpv6TrafficClass(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = false,
              };
            });
          case IPV6_MULTICAST_IF:
            return proc.Process(client().GetIpv6MulticastInterface(), [](const auto& response) {
              return static_cast<uint32_t>(response.value);
            });
          case IPV6_MULTICAST_HOPS:
            return proc.Process(client().GetIpv6MulticastHops(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = false,
              };
            });
          case IPV6_MULTICAST_LOOP:
            return proc.Process(client().GetIpv6MulticastLoopback(), [](const auto& response) {
              return PartialCopy{
                  .value = response.value,
                  .allow_char = false,
              };
            });
          case IPV6_RECVTCLASS:
            return proc.Process(client().GetIpv6ReceiveTrafficClass(), [](const auto& response) {
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
              return proc.Process(client().GetTcpNoDelay(),
                                  [](const auto& response) { return response.value; });
            case TCP_CORK:
              return proc.Process(client().GetTcpCork(),
                                  [](const auto& response) { return response.value; });
            case TCP_QUICKACK:
              return proc.Process(client().GetTcpQuickAck(),
                                  [](const auto& response) { return response.value; });
            case TCP_MAXSEG:
              return proc.Process(client().GetTcpMaxSegment(),
                                  [](const auto& response) { return response.value_bytes; });
            case TCP_KEEPIDLE:
              return proc.Process(client().GetTcpKeepAliveIdle(),
                                  [](const auto& response) { return response.value_secs; });
            case TCP_KEEPINTVL:
              return proc.Process(client().GetTcpKeepAliveInterval(),
                                  [](const auto& response) { return response.value_secs; });
            case TCP_KEEPCNT:
              return proc.Process(client().GetTcpKeepAliveCount(),
                                  [](const auto& response) { return response.value; });
            case TCP_USER_TIMEOUT:
              return proc.Process(client().GetTcpUserTimeout(),
                                  [](const auto& response) { return response.value_millis; });
            case TCP_CONGESTION:
              return proc.Process(client().GetTcpCongestion(), [](const auto& response) {
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
              return proc.Process(client().GetTcpDeferAccept(),
                                  [](const auto& response) { return response.value_secs; });
            case TCP_INFO:
              return proc.Process(
                  client().GetTcpInfo(),
                  [](const auto& response) -> const auto& { return response.info; });
            case TCP_SYNCNT:
              return proc.Process(client().GetTcpSynCount(),
                                  [](const auto& response) { return response.value; });
            case TCP_WINDOW_CLAMP:
              return proc.Process(client().GetTcpWindowClamp(),
                                  [](const auto& response) { return response.value; });
            case TCP_LINGER2:
              return proc.Process(client().GetTcpLinger(),
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
        switch (optname) {
          case SO_TIMESTAMP:
            return proc.Process<bool>([this](bool value) { return client().SetTimestamp(value); });
          case SO_SNDBUF:
            return proc.Process<int32_t>([this](int32_t value) {
              // NB: SNDBUF treated as unsigned, we just cast the value to skip sign check.
              return client().SetSendBuffer(static_cast<uint64_t>(value));
            });
          case SO_RCVBUF:
            // NB: RCVBUF treated as unsigned, we just cast the value to skip sign check.
            return proc.Process<int32_t>([this](int32_t value) {
              return client().SetReceiveBuffer(static_cast<uint64_t>(value));
            });
          case SO_REUSEADDR:
            return proc.Process<bool>(
                [this](bool value) { return client().SetReuseAddress(value); });
          case SO_REUSEPORT:
            return proc.Process<bool>([this](bool value) { return client().SetReusePort(value); });
          case SO_BINDTODEVICE:
            return proc.Process<fidl::StringView>(
                [this](fidl::StringView value) { return client().SetBindToDevice(value); });
          case SO_BROADCAST:
            return proc.Process<bool>([this](bool value) { return client().SetBroadcast(value); });
          case SO_KEEPALIVE:
            return proc.Process<bool>([this](bool value) { return client().SetKeepAlive(value); });
          case SO_LINGER:
            return proc.Process<struct linger>([this](struct linger value) {
              // NB: l_linger is typed as int but interpreted as unsigned by linux.
              return client().SetLinger(value.l_onoff != 0, static_cast<uint32_t>(value.l_linger));
            });
          case SO_OOBINLINE:
            return proc.Process<bool>(
                [this](bool value) { return client().SetOutOfBandInline(value); });
          case SO_NO_CHECK:
            return proc.Process<bool>([this](bool value) { return client().SetNoCheck(value); });
          case SO_SNDTIMEO:
          case SO_RCVTIMEO:
            return SockOptResult::Errno(ENOTSUP);
          default:
            return SockOptResult::Errno(ENOPROTOOPT);
        }
      case SOL_IP:
        switch (optname) {
          case IP_MULTICAST_TTL:
            return proc.Process<OptionalUint8CharAllowed>([this](OptionalUint8CharAllowed value) {
              return client().SetIpMulticastTtl(value.inner.opt);
            });
          case IP_ADD_MEMBERSHIP: {
            return proc.Process<fsocket::wire::IpMulticastMembership>(
                [this](fsocket::wire::IpMulticastMembership value) {
                  return client().AddIpMembership(value);
                });
          }
          case IP_DROP_MEMBERSHIP:
            return proc.Process<fsocket::wire::IpMulticastMembership>(
                [this](fsocket::wire::IpMulticastMembership value) {
                  return client().DropIpMembership(value);
                });
          case IP_MULTICAST_IF: {
            if (optlen == sizeof(struct in_addr)) {
              return proc.Process<struct in_addr>([this](struct in_addr value) {
                fnet::wire::Ipv4Address addr;
                std::copy_n(reinterpret_cast<const uint8_t*>(&value.s_addr), sizeof(value.s_addr),
                            addr.addr.begin());
                return client().SetIpMulticastInterface(0, addr);
              });
            }
            return proc.Process<fsocket::wire::IpMulticastMembership>(
                [this](fsocket::wire::IpMulticastMembership value) {
                  return client().SetIpMulticastInterface(value.iface, value.local_addr);
                });
          }
          case IP_MULTICAST_LOOP:
            return proc.Process<IntOrChar>([this](IntOrChar value) {
              return client().SetIpMulticastLoopback(value.value != 0);
            });
          case IP_TTL:
            return proc.Process<OptionalUint8>(
                [this](OptionalUint8 value) { return client().SetIpTtl(value.opt); });
          case IP_TOS:
            if (optlen == 0) {
              return SockOptResult::Ok();
            }
            return proc.Process<IntOrChar>([this](IntOrChar value) {
              return client().SetIpTypeOfService(static_cast<uint8_t>(value.value));
            });
          case IP_RECVTOS:
            return proc.Process<IntOrChar>([this](IntOrChar value) {
              return client().SetIpReceiveTypeOfService(value.value != 0);
            });
          case IP_PKTINFO:
            return proc.Process<IntOrChar>(
                [this](IntOrChar value) { return client().SetIpPacketInfo(value.value != 0); });
          case MCAST_JOIN_GROUP:
            return SockOptResult::Errno(ENOTSUP);
          default:
            return SockOptResult::Errno(ENOPROTOOPT);
        }
      case SOL_IPV6:
        switch (optname) {
          case IPV6_V6ONLY:
            return proc.Process<bool>([this](bool value) { return client().SetIpv6Only(value); });
          case IPV6_ADD_MEMBERSHIP:
            return proc.Process<fsocket::wire::Ipv6MulticastMembership>(
                [this](fsocket::wire::Ipv6MulticastMembership value) {
                  return client().AddIpv6Membership(value);
                });
          case IPV6_DROP_MEMBERSHIP:
            return proc.Process<fsocket::wire::Ipv6MulticastMembership>(
                [this](fsocket::wire::Ipv6MulticastMembership value) {
                  return client().DropIpv6Membership(value);
                });
          case IPV6_MULTICAST_IF:
            return proc.Process<IntOrChar>([this](IntOrChar value) {
              return client().SetIpv6MulticastInterface(value.value);
            });
          case IPV6_MULTICAST_HOPS:
            return proc.Process<OptionalUint8>(
                [this](OptionalUint8 value) { return client().SetIpv6MulticastHops(value.opt); });
          case IPV6_MULTICAST_LOOP:
            return proc.Process<bool>(
                [this](bool value) { return client().SetIpv6MulticastLoopback(value); });
          case IPV6_TCLASS:
            return proc.Process<OptionalUint8>(
                [this](OptionalUint8 value) { return client().SetIpv6TrafficClass(value.opt); });
          case IPV6_RECVTCLASS:
            return proc.Process<bool>(
                [this](bool value) { return client().SetIpv6ReceiveTrafficClass(value); });
          default:
            return SockOptResult::Errno(ENOPROTOOPT);
        }
      case SOL_TCP:
        if constexpr (std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>>) {
          switch (optname) {
            case TCP_NODELAY:
              return proc.Process<bool>(
                  [this](bool value) { return client().SetTcpNoDelay(value); });
            case TCP_CORK:
              return proc.Process<bool>([this](bool value) { return client().SetTcpCork(value); });
            case TCP_QUICKACK:
              return proc.Process<bool>(
                  [this](bool value) { return client().SetTcpQuickAck(value); });
            case TCP_MAXSEG:
              return proc.Process<uint32_t>(
                  [this](uint32_t value) { return client().SetTcpMaxSegment(value); });
            case TCP_KEEPIDLE:
              return proc.Process<uint32_t>(
                  [this](uint32_t value) { return client().SetTcpKeepAliveIdle(value); });
            case TCP_KEEPINTVL:
              return proc.Process<uint32_t>(
                  [this](uint32_t value) { return client().SetTcpKeepAliveInterval(value); });
            case TCP_KEEPCNT:
              return proc.Process<uint32_t>(
                  [this](uint32_t value) { return client().SetTcpKeepAliveCount(value); });
            case TCP_USER_TIMEOUT:
              return proc.Process<uint32_t>(
                  [this](uint32_t value) { return client().SetTcpUserTimeout(value); });
            case TCP_CONGESTION:
              return proc.Process<fsocket::wire::TcpCongestionControl>(
                  [this](fsocket::wire::TcpCongestionControl value) {
                    return client().SetTcpCongestion(value);
                  });
            case TCP_DEFER_ACCEPT:
              return proc.Process<int32_t>([this](int32_t value) {
                if (value < 0) {
                  value = 0;
                }
                return client().SetTcpDeferAccept(value);
              });
            case TCP_SYNCNT:
              return proc.Process<uint32_t>(
                  [this](uint32_t value) { return client().SetTcpSynCount(value); });
            case TCP_WINDOW_CLAMP:
              return proc.Process<uint32_t>(
                  [this](uint32_t value) { return client().SetTcpWindowClamp(value); });
            case TCP_LINGER2:
              return proc.Process<int32_t>([this](int32_t value) {
                OptionalUint32 opt;
                if (value < 0) {
                  opt.set_unset();
                } else {
                  opt.set_value(static_cast<uint32_t>(value));
                }
                return client().SetTcpLinger(opt.opt);
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

    if (use_legacy_shutdown2_fidl()) {
      auto response = client().Shutdown2(mode);
      zx_status_t status = response.status();
      if (status != ZX_OK) {
        return status;
      }
      auto const& result = response.Unwrap()->result;
      if (result.is_err()) {
        *out_code = static_cast<int16_t>(result.err());
        return ZX_OK;
      }
      *out_code = 0;
      return ZX_OK;
    }

    auto response = client().Shutdown(mode);
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.Unwrap()->result;
    if (result.is_err()) {
      *out_code = static_cast<int16_t>(result.err());
      return ZX_OK;
    }
    *out_code = 0;
    return ZX_OK;
  }

 private:
  T& client_;
};

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

template <typename F>
Errno zxsio_posix_ioctl(int req, va_list va, F fallback) {
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
      auto const& result = response.Unwrap()->result;
      if (result.is_err()) {
        if (result.err() == ZX_ERR_NOT_FOUND) {
          return Errno(ENODEV);
        }
        return Errno(fdio_status_to_errno(result.err()));
      }
      auto const& name = result.response().name;
      const size_t n = std::min(name.size(), sizeof(ifr->ifr_name));
      memcpy(ifr->ifr_name, name.data(), n);
      ifr->ifr_name[n] = 0;
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
      auto const& result = response.Unwrap()->result;
      if (result.is_err()) {
        if (result.err() == ZX_ERR_NOT_FOUND) {
          return Errno(ENODEV);
        }
        return Errno(fdio_status_to_errno(result.err()));
      }
      ifr->ifr_ifindex = static_cast<int>(result.response().index);
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
      auto const& result = response.Unwrap()->result;
      if (result.is_err()) {
        if (result.err() == ZX_ERR_NOT_FOUND) {
          return Errno(ENODEV);
        }
        return Errno(fdio_status_to_errno(result.err()));
      }
      ifr->ifr_flags =
          static_cast<uint16_t>(result.response().flags);  // NOLINT(bugprone-narrowing-conversions)
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
      const auto& interfaces = response.Unwrap()->interfaces;

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
            if (address.addr.which() == fnet::wire::IpAddress::Tag::kIpv4) {
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
          if (addr.which() != fnet::wire::IpAddress::Tag::kIpv4) {
            continue;
          }

          // Write interface name.
          size_t len = std::min(if_name.size(), sizeof(ifr->ifr_name) - 1);
          memcpy(ifr->ifr_name, if_name.data(), len);
          ifr->ifr_name[len] = 0;

          // Write interface address.
          auto& s = *reinterpret_cast<struct sockaddr_in*>(&ifr->ifr_addr);
          const auto& ipv4 = addr.ipv4();
          s.sin_family = AF_INET;
          s.sin_port = 0;
          std::copy(ipv4.addr.begin(), ipv4.addr.end(), reinterpret_cast<uint8_t*>(&s.sin_addr));

          ifr++;
        }
      }
      ifc.ifc_len = static_cast<int>((ifr - ifc.ifc_req) * sizeof(struct ifreq));
      return Errno(Errno::Ok);
    }
    default:
      return fallback(req, va);
  }
}

}  // namespace

namespace fdio_internal {

struct DatagramSocket {
  using zxio_type = zxio_datagram_socket_t;

  static fsocket::wire::SendControlData empty_control_data() {
    return fsocket::wire::SendControlData();
  }
};

struct RawSocket {
  using zxio_type = zxio_raw_socket_t;

  static frawsocket::wire::SendControlData empty_control_data() {
    return frawsocket::wire::SendControlData();
  }
};

template <typename T, typename = std::enable_if_t<std::is_same_v<T, DatagramSocket> ||
                                                  std::is_same_v<T, RawSocket>>>
struct socket_with_event : public zxio {
  static constexpr zx_signals_t kSignalIncoming = ZX_USER_SIGNAL_0;
  static constexpr zx_signals_t kSignalOutgoing = ZX_USER_SIGNAL_1;
  static constexpr zx_signals_t kSignalError = ZX_USER_SIGNAL_2;
  static constexpr zx_signals_t kSignalShutdownRead = ZX_USER_SIGNAL_4;
  static constexpr zx_signals_t kSignalShutdownWrite = ZX_USER_SIGNAL_5;

  void wait_begin(uint32_t events, zx_handle_t* handle, zx_signals_t* out_signals) override {
    *handle = zxio_socket_with_event().event.get();

    zx_signals_t signals = ZX_EVENTPAIR_PEER_CLOSED | kSignalError;
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
    if (signals & (ZX_EVENTPAIR_PEER_CLOSED | kSignalError)) {
      events |= POLLERR;
    }
    if (signals & (ZX_EVENTPAIR_PEER_CLOSED | kSignalShutdownRead)) {
      events |= POLLRDHUP;
    }
    *out_events = events;
  }

  Errno posix_ioctl(int req, va_list va) final {
    return zxsio_posix_ioctl(req, va,
                             [this](int req, va_list va) { return zxio::posix_ioctl(req, va); });
  }

  zx_status_t bind(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) override {
    return BaseSocket(zxio_socket_with_event().client).bind(addr, addrlen, out_code);
  }

  zx_status_t connect(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) override {
    return BaseSocket(zxio_socket_with_event().client).connect(addr, addrlen, out_code);
  }

  zx_status_t listen(int backlog, int16_t* out_code) override { return ZX_ERR_WRONG_TYPE; }

  zx_status_t accept(int flags, struct sockaddr* addr, socklen_t* addrlen, zx_handle_t* out_handle,
                     int16_t* out_code) override {
    return ZX_ERR_WRONG_TYPE;
  }

  zx_status_t getsockname(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) override {
    return BaseSocket(zxio_socket_with_event().client).getsockname(addr, addrlen, out_code);
  }

  zx_status_t getpeername(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) override {
    return BaseSocket(zxio_socket_with_event().client).getpeername(addr, addrlen, out_code);
  }

  zx_status_t getsockopt(int level, int optname, void* optval, socklen_t* optlen,
                         int16_t* out_code) override {
    SockOptResult result =
        BaseSocket(zxio_socket_with_event().client).getsockopt_fidl(level, optname, optval, optlen);
    *out_code = result.err;
    return result.status;
  }

  zx_status_t setsockopt(int level, int optname, const void* optval, socklen_t optlen,
                         int16_t* out_code) override {
    SockOptResult result =
        BaseSocket(zxio_socket_with_event().client).setsockopt_fidl(level, optname, optval, optlen);
    *out_code = result.err;
    return result.status;
  }

  zx_status_t recvmsg(struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    size_t datalen = 0;
    for (int i = 0; i < msg->msg_iovlen; ++i) {
      datalen += msg->msg_iov[i].iov_len;
    }

    bool want_addr = msg->msg_namelen != 0 && msg->msg_name != nullptr;
    auto response = zxio_socket_with_event().client.RecvMsg(
        want_addr, static_cast<uint32_t>(datalen), false, to_recvmsg_flags(flags));
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.Unwrap()->result;
    if (result.is_err()) {
      *out_code = static_cast<int16_t>(result.err());
      return ZX_OK;
    }
    *out_code = 0;

    {
      auto const& out = result.response().addr;
      // Result address has invalid tag when it's not provided by the server (when want_addr
      // is false).
      // TODO(https://fxbug.dev/58503): Use better representation of nullable union when available.
      if (want_addr && !out.has_invalid_tag()) {
        msg->msg_namelen = static_cast<socklen_t>(
            fidl_to_sockaddr(out, static_cast<struct sockaddr*>(msg->msg_name), msg->msg_namelen));
      }
    }

    {
      auto const& out = result.response().data;

      const uint8_t* data = out.begin();
      size_t remaining = out.count();
      for (int i = 0; remaining != 0 && i < msg->msg_iovlen; ++i) {
        auto const& iov = msg->msg_iov[i];
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
      if (result.response().truncated != 0) {
        msg->msg_flags |= MSG_TRUNC;
      } else {
        msg->msg_flags &= ~MSG_TRUNC;
      }
      size_t actual = out.count() - remaining;
      if ((flags & MSG_TRUNC) != 0) {
        actual += result.response().truncated;
      }
      *out_actual = actual;
    }
    // TODO(https://fxbug.dev/21106): Support control messages.
    msg->msg_controllen = 0;

    return ZX_OK;
  }

  zx_status_t sendmsg(const struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    SocketAddress addr;
    // Attempt to load socket address if either name or namelen is set.
    // If only one is set, it'll result in INVALID_ARGS.
    if (msg->msg_namelen != 0 || msg->msg_name != nullptr) {
      zx_status_t status =
          addr.LoadSockAddr(static_cast<struct sockaddr*>(msg->msg_name), msg->msg_namelen);
      if (status != ZX_OK) {
        return status;
      }
    }

    size_t total = 0;
    for (int i = 0; i < msg->msg_iovlen; ++i) {
      auto const& iov = msg->msg_iov[i];
      if (iov.iov_base == nullptr && iov.iov_len != 0) {
        *out_code = EFAULT;
        return ZX_OK;
      }
      total += iov.iov_len;
    }

    std::vector<uint8_t> data;
    auto vec = fidl::VectorView<uint8_t>();
    switch (msg->msg_iovlen) {
      case 0: {
        break;
      }
      case 1: {
        auto const& iov = *msg->msg_iov;
        vec = fidl::VectorView<uint8_t>::FromExternal(static_cast<uint8_t*>(iov.iov_base),
                                                      iov.iov_len);
        break;
      }
      default: {
        // TODO(https://fxbug.dev/67928): avoid this copy.
        data.reserve(total);
        for (int i = 0; i < msg->msg_iovlen; ++i) {
          auto const& iov = msg->msg_iov[i];
          std::copy_n(static_cast<const uint8_t*>(iov.iov_base), iov.iov_len,
                      std::back_inserter(data));
        }
        vec = fidl::VectorView<uint8_t>::FromExternal(data);
      }
    }

    // TODO(https://fxbug.dev/21106): Support control messages.
    // TODO(https://fxbug.dev/58503): Use better representation of nullable union when
    // available. Currently just using a default-initialized union with an invalid tag.
    auto response = zxio_socket_with_event().client.SendMsg(
        addr.address, vec, T::empty_control_data(), to_sendmsg_flags(flags));
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.Unwrap()->result;
    if (result.is_err()) {
      *out_code = static_cast<int16_t>(result.err());
      return ZX_OK;
    }
    *out_code = 0;
    *out_actual = result.response().len;
    return ZX_OK;
  }

  zx_status_t shutdown(int how, int16_t* out_code) override {
    return BaseSocket(zxio_socket_with_event().client).shutdown(how, out_code);
  }

 protected:
  friend class fbl::internal::MakeRefCountedHelper<socket_with_event<T>>;
  friend class fbl::RefPtr<socket_with_event<T>>;

  socket_with_event<T>() = default;
  ~socket_with_event<T>() override = default;

 private:
  typename T::zxio_type& zxio_socket_with_event() {
    return *reinterpret_cast<typename T::zxio_type*>(&zxio_storage().io);
  }
};

using datagram_socket = socket_with_event<DatagramSocket>;
using raw_socket = socket_with_event<RawSocket>;

}  // namespace fdio_internal

zx::status<fdio_ptr> fdio_datagram_socket_create(zx::eventpair event,
                                                 fidl::ClientEnd<fsocket::DatagramSocket> client) {
  fdio_ptr io = fbl::MakeRefCounted<fdio_internal::datagram_socket>();
  if (io == nullptr) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  zx_status_t status =
      zxio_datagram_socket_init(&io->zxio_storage(), std::move(event), std::move(client));
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(io);
}

zx::status<fdio_ptr> fdio_raw_socket_create(zx::eventpair event,
                                            fidl::ClientEnd<frawsocket::Socket> client) {
  fdio_ptr io = fbl::MakeRefCounted<fdio_internal::raw_socket>();
  if (io == nullptr) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  zx_status_t status =
      zxio_raw_socket_init(&io->zxio_storage(), std::move(event), std::move(client));
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(io);
}

static zxio_stream_socket_t& zxio_stream_socket(zxio_t* io) {
  return *reinterpret_cast<zxio_stream_socket_t*>(io);
}

namespace fdio_internal {

struct stream_socket : public zxio {
  static constexpr zx_signals_t kSignalIncoming = ZX_USER_SIGNAL_0;
  static constexpr zx_signals_t kSignalConnected = ZX_USER_SIGNAL_3;

  enum class State {
    kUnconnected,
    kListening,
    kConnecting,
    kConnected,
  };

  void wait_begin(uint32_t events, zx_handle_t* handle, zx_signals_t* out_signals) override {
    zxio_signals_t signals = ZXIO_SIGNAL_PEER_CLOSED;

    auto [state, has_error] = GetState();
    switch (state) {
      case State::kUnconnected:
        // Stream sockets which are non-listening or unconnected do not have a potential peer
        // to generate any waitable signals, skip signal waiting and notify the caller of the
        // same.
        *out_signals = ZX_SIGNAL_NONE;
        return;
      case State::kListening:
        break;
      case State::kConnecting:
        if (events & POLLIN) {
          signals |= ZXIO_SIGNAL_READABLE;
        }
        break;
      case State::kConnected:
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
      std::lock_guard lock(state_lock_);
      auto [state, has_error] = StateLocked();
      switch (state) {
        case State::kUnconnected:
          ZX_ASSERT_MSG(zx_signals == ZX_SIGNAL_NONE, "zx_signals=%s on unconnected socket",
                        std::bitset<sizeof(zx_signals)>(zx_signals).to_string().c_str());
          *out_events = POLLOUT | POLLHUP;
          return;

        case State::kListening:
          if (zx_signals & kSignalIncoming) {
            events |= POLLIN;
          }
          use_inner = false;
          break;
        case State::kConnecting:
          if (zx_signals & kSignalConnected) {
            state_ = State::kConnected;
            events |= POLLOUT;
          }
          zx_signals &= ~kSignalConnected;
          use_inner = false;
          break;
        case State::kConnected:
          use_inner = true;
          break;
      }
    }

    if (use_inner) {
      wait_end_inner(zx_signals, &events, &signals);
    } else {
      zxio_wait_end(&zxio_storage().io, zx_signals, &signals);
    }

    if (signals & ZXIO_SIGNAL_PEER_CLOSED) {
      events |= POLLIN | POLLOUT | POLLERR | POLLHUP | POLLRDHUP;
    }
    if (signals & ZXIO_SIGNAL_WRITE_DISABLED) {
      events |= POLLHUP | POLLOUT;
    }
    if (signals & ZXIO_SIGNAL_READ_DISABLED) {
      events |= POLLRDHUP | POLLIN;
    }
    *out_events = events;
  }

  Errno posix_ioctl(int req, va_list va) final {
    return zxsio_posix_ioctl(req, va,
                             [this](int req, va_list va) { return zxio::posix_ioctl(req, va); });
  }

  zx_status_t bind(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) override {
    return BaseSocket(zxio_stream_socket().client).bind(addr, addrlen, out_code);
  }

  zx_status_t connect(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) override {
    zx_status_t status = BaseSocket(zxio_stream_socket().client).connect(addr, addrlen, out_code);
    if (status == ZX_OK) {
      std::lock_guard lock(state_lock_);
      switch (*out_code) {
        case 0:
          state_ = State::kConnected;
          break;
        case EINPROGRESS:
          state_ = State::kConnecting;
          break;
      }
    }
    return status;
  }

  zx_status_t listen(int backlog, int16_t* out_code) override {
    auto response = zxio_stream_socket().client.Listen(safemath::saturated_cast<int16_t>(backlog));
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.Unwrap()->result;
    if (result.is_err()) {
      *out_code = static_cast<int16_t>(result.err());
      return ZX_OK;
    }
    {
      std::lock_guard lock(state_lock_);
      state_ = State::kListening;
    }
    *out_code = 0;
    return ZX_OK;
  }

  zx_status_t accept(int flags, struct sockaddr* addr, socklen_t* addrlen, zx_handle_t* out_handle,
                     int16_t* out_code) override {
    bool want_addr = addr != nullptr && addrlen != nullptr;
    auto response = zxio_stream_socket().client.Accept(want_addr);
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto& result = response.Unwrap()->result;
    if (result.is_err()) {
      *out_code = static_cast<int16_t>(result.err());
      return ZX_OK;
    }
    *out_code = 0;
    *out_handle = result.mutable_response().s.channel().release();
    auto const& out = result.response().addr;
    // Result address has invalid tag when it's not provided by the server (when want_addr
    // is false).
    // TODO(https://fxbug.dev/58503): Use better representation of nullable union when available.
    if (want_addr && !out.has_invalid_tag()) {
      *addrlen = static_cast<socklen_t>(fidl_to_sockaddr(out, addr, *addrlen));
    }
    return ZX_OK;
  }

  zx_status_t getsockname(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) override {
    return BaseSocket(zxio_stream_socket().client).getsockname(addr, addrlen, out_code);
  }

  zx_status_t getpeername(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) override {
    return BaseSocket(zxio_stream_socket().client).getpeername(addr, addrlen, out_code);
  }

  zx_status_t getsockopt(int level, int optname, void* optval, socklen_t* optlen,
                         int16_t* out_code) override {
    SockOptResult result =
        BaseSocket(zxio_stream_socket().client).getsockopt_fidl(level, optname, optval, optlen);
    *out_code = result.err;
    return result.status;
  }

  zx_status_t setsockopt(int level, int optname, const void* optval, socklen_t optlen,
                         int16_t* out_code) override {
    SockOptResult result =
        BaseSocket(zxio_stream_socket().client).setsockopt_fidl(level, optname, optval, optlen);
    *out_code = result.err;
    return result.status;
  }

  zx_status_t recvmsg(struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    zx::status preflight = Preflight(ENOTCONN);
    if (preflight.is_error()) {
      return preflight.status_value();
    }
    if (std::optional err = preflight.value(); err.has_value()) {
      *out_code = err.value();
      return ZX_OK;
    }

    zx_status_t status = recvmsg_inner(msg, flags, out_actual);
    switch (status) {
      case ZX_ERR_INVALID_ARGS:
        *out_code = EFAULT;
        return ZX_OK;
      case ZX_ERR_BAD_STATE:
        __FALLTHROUGH;
      case ZX_ERR_PEER_CLOSED: {
        zx::status err = GetError();
        if (err.is_error()) {
          return err.status_value();
        }
        *out_actual = 0;
        *out_code = err.value();
        return ZX_OK;
      }
      default:
        *out_code = 0;
        return status;
    }
  }

  zx_status_t sendmsg(const struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    zx::status preflight = Preflight(EPIPE);
    if (preflight.is_error()) {
      return preflight.status_value();
    }
    if (std::optional err = preflight.value(); err.has_value()) {
      *out_code = err.value();
      return ZX_OK;
    }

    // TODO(https://fxbug.dev/21106): support flags and control messages
    zx_status_t status = sendmsg_inner(msg, flags, out_actual);
    switch (status) {
      case ZX_ERR_INVALID_ARGS:
        *out_code = EFAULT;
        return ZX_OK;
      case ZX_ERR_BAD_STATE:
        __FALLTHROUGH;
      case ZX_ERR_PEER_CLOSED: {
        zx::status err = GetError();
        if (err.is_error()) {
          return err.status_value();
        }
        if (int value = err.value(); value != 0) {
          *out_code = value;
          return ZX_OK;
        }

        // Error was consumed.
        *out_code = EPIPE;
        return ZX_OK;
      }
      default:
        *out_code = 0;
        return status;
    }
  }

  zx_status_t shutdown(int how, int16_t* out_code) override {
    return BaseSocket(zxio_stream_socket().client).shutdown(how, out_code);
  }

 private:
  zxio_stream_socket_t& zxio_stream_socket() { return ::zxio_stream_socket(&zxio_storage().io); }

  zx::status<std::optional<int32_t>> Preflight(int fallback) {
    auto [state, has_error] = GetState();
    if (has_error) {
      zx::status err = GetError();
      if (err.is_error()) {
        return err.take_error();
      }
      if (int value = err.value(); value != 0) {
        return zx::ok(value);
      }
      // Error was consumed.
    }

    switch (state) {
      case State::kUnconnected:
        __FALLTHROUGH;
      case State::kListening:
        return zx::ok(fallback);
      case State::kConnecting:
        if (!has_error) {
          return zx::ok(EAGAIN);
        }
        // There's an error on the socket, we will discover it when we perform our I/O.
        __FALLTHROUGH;
      case State::kConnected:
        return zx::ok(std::nullopt);
    }
  }

  zx::status<int32_t> GetError() {
    fidl::WireResult response = zxio_stream_socket().client.GetError();
    if (!response.ok()) {
      return zx::error(response.status());
    }
    fsocket::wire::BaseSocketGetErrorResult result = response.value().result;
    switch (result.which()) {
      case fsocket::wire::BaseSocketGetErrorResult::Tag::kResponse:
        return zx::ok(0);
      case fsocket::wire::BaseSocketGetErrorResult::Tag::kErr:
        return zx::ok(static_cast<int32_t>(result.err()));
    }
  }

  std::mutex state_lock_;
  State state_ __TA_GUARDED(state_lock_);

  std::pair<State, bool> StateLocked() __TA_REQUIRES(state_lock_) {
    switch (state_) {
      case State::kUnconnected:
        __FALLTHROUGH;
      case State::kListening:
        return std::make_pair(state_, false);
      case State::kConnecting: {
        zx_signals_t observed;
        zx_status_t status = zxio_stream_socket().pipe.socket.wait_one(
            kSignalConnected, zx::time::infinite_past(), &observed);
        switch (status) {
          case ZX_OK:
            if (observed & kSignalConnected) {
              state_ = State::kConnected;
            }
            __FALLTHROUGH;
          case ZX_ERR_TIMED_OUT:
            return std::make_pair(state_, observed & ZX_SOCKET_PEER_CLOSED);
          default:
            ZX_PANIC("ASSERT FAILED at (%s:%d): status=%s\n", __FILE__, __LINE__,
                     zx_status_get_string(status));
        }
        break;
      }
      case State::kConnected:
        return std::make_pair(state_, false);
    }
  }

  std::pair<State, bool> GetState() __TA_EXCLUDES(state_lock_) {
    std::lock_guard lock(state_lock_);
    return StateLocked();
  }

 protected:
  friend class fbl::internal::MakeRefCountedHelper<stream_socket>;
  friend class fbl::RefPtr<stream_socket>;

  explicit stream_socket(State state) : state_(state) {}
  ~stream_socket() override = default;
};

}  // namespace fdio_internal

zx::status<fdio_ptr> fdio_stream_socket_create(zx::socket socket,
                                               fidl::ClientEnd<fsocket::StreamSocket> client) {
  zx_info_socket_t info;
  if (zx_status_t status = socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
      status != ZX_OK) {
    return zx::error(status);
  }
  zx::status state = [&socket]() -> zx::status<fdio_internal::stream_socket::State> {
    zx_status_t status = socket.wait_one(fdio_internal::stream_socket::kSignalConnected,
                                         zx::time::infinite_past(), nullptr);
    // TODO(tamird): Transferring a listening or connecting socket to another process doesn't work
    // correctly since those states can't be observed here.
    switch (status) {
      case ZX_OK:
        return zx::ok(fdio_internal::stream_socket::State::kConnected);
      case ZX_ERR_TIMED_OUT:
        return zx::ok(fdio_internal::stream_socket::State::kUnconnected);
      default:
        return zx::error(status);
    }
  }();
  if (state.is_error()) {
    return state.take_error();
  }

  fdio_ptr io = fbl::MakeRefCounted<fdio_internal::stream_socket>(state.value());
  if (io == nullptr) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  zx_status_t status =
      zxio_stream_socket_init(&io->zxio_storage(), std::move(socket), std::move(client), info);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(io);
}

bool fdio_is_socket(fdio_t* io) {
  if (!io) {
    return false;
  }
  bool is_socket = false;
  if (zxio_is_socket(&io->zxio_storage().io, &is_socket) != ZX_OK) {
    return false;
  }
  return is_socket;
}
