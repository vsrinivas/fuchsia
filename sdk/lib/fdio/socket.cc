// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdk/lib/fdio/socket.h"

#include <lib/fit/result.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/socket.h>
#include <lib/zxio/bsdsocket.h>
#include <lib/zxio/cpp/cmsg.h>
#include <lib/zxio/cpp/create_with_type.h>
#include <lib/zxio/cpp/dgram_cache.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/cpp/socket_address.h>
#include <lib/zxio/cpp/transitional.h>
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
 *  Stateful class hierarchy for wrapping zircon primitives, enabled for
 *  relevant FIDL wrappers:
 *
 *                                                +-----------------+ +-----------------+
 *          +---------------+                     |  stream_socket  | | datagram_socket |
 *          | packet_socket |                     |                 | |                 |
 *          |               |                     |    Enabled:     | |    Enabled:     |
 *          |   Enabled:    |                     |   StreamSocket  | | DatagramSocket  |
 *          |  PacketSocket |                     |                 | |                 |
 *          |               |                     |    Implements:  | |    Implements:  |
 *          |  Implements:  |                     |  Overrides for  | |  Overrides for  |
 *          | Overrides for |                     |   SOCK_STREAM   | |   SOCK_DGRAM    |
 *          |    packet     |                     |  sockets using  | |  sockets using  |
 *          |    sockets    |                     |  a zx::socket   | |  a zx::socket   |
 *          |  (AF_PACKET)  |                     |   data plane    | |   data plane    |
 *          +---------------+                     +-----------------+ +-----------------+
 *                   ^                                      ^                   ^
 *                   |                                      |                   |
 *                   |                                      |                   |
 *                   |                                      +---------+---------+
 *                   |                                                |
 *                   |                                                |
 *       +-----------+-----------+                       +------------+-------------+
 *       |   socket_with_event   |                       |   socket_with_zx_socket  |
 *       |                       |                       |                          |
 *       |       Enabled:        |                       |         Enabled:         |
 *       |     PacketSocket      |                       |       DatagramSocket     |
 *       |       RawSocket       |                       |        StreamSocket      |
 *       |    SyncDgramSocket    |                       |                          |
 *       |                       |                       |   Implements: Overrides  |
 *       | Implements: Overrides |                       |    for sockets using a   |
 *       |   for sockets using   |                       |   zx::socket data plane  |
 *       |   FIDL over channel   |                       |                          |
 *       |    as a data plane    |                       |                          |
 *       +-----------------------+                       +--------------------------+
 *                    ^                                               ^
 *                    |                                               |
 *                    |                                               |
 *                    +----------------------+------------------------+
 *                                           |
 *                                           |
 *                                +----------+---------+
 *                                |     base_socket    |
 *                                |                    |
 *                                |    Enabled: All    |
 *                                |                    |
 *                                |    Implements:     |
 *                                | Overrides for all  |
 *                                |    socket types    |
 *                                +--------------------+
 *                                           |
 *                                +----------+-----------+
 *                                |         zxio         |
 *                                |                      |
 *                                |  Implements: POSIX   |
 *                                | interface + behavior |
 *                                |    for generic fds   |
 *                                +----------------------+
 */

namespace {

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
struct base_socket : public remote {
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

struct StreamSocket {
  using FidlProtocol = fsocket::StreamSocket;
};

struct DatagramSocket {
  using FidlProtocol = fsocket::DatagramSocket;
};

struct SynchronousDatagramSocket {
  using zxio_type = zxio_synchronous_datagram_socket_t;
  using FidlProtocol = zxio_type::FidlProtocol;
};

struct RawSocket {
  using zxio_type = zxio_raw_socket_t;
  using FidlProtocol = zxio_type::FidlProtocol;
};

struct PacketSocket {
  using zxio_type = zxio_packet_socket_t;
  using FidlProtocol = zxio_type::FidlProtocol;
};

template <typename T, typename = std::enable_if_t<std::is_same_v<T, SynchronousDatagramSocket> ||
                                                  std::is_same_v<T, RawSocket> ||
                                                  std::is_same_v<T, PacketSocket>>>
// inheritance is virtual to avoid multiple copies of `base_socket<T>` when derived classes
// inherit from `socket_with_event`.
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
    return zxio_recvmsg(&zxio_socket_with_event().io, msg, flags, out_actual, out_code);
  }

  zx_status_t sendmsg(const struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    return zxio_sendmsg(&zxio_socket_with_event().io, msg, flags, out_actual, out_code);
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

using synchronous_datagram_socket = socket_with_event<SynchronousDatagramSocket>;
using raw_socket = socket_with_event<RawSocket>;

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
struct socket_with_zx_socket : public base_socket<T> {
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
        zxio_datagram_socket().cmsg_cache.Get(socket_err_wait_item(), cmsg_requested, GetClient());
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
    *out_actual =
        zxio_set_trunc_flags_and_return_out_actual(*msg, payload_bytes_read, truncated, flags);

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
    // TODO(https://fxbug.dev/110570) Add tests with msg as nullptr.
    if (msg == nullptr) {
      *out_code = EFAULT;
      return ZX_OK;
    }
    const msghdr& msghdr_ref = *msg;
    std::optional opt_total = zxio_total_iov_len(msghdr_ref);
    if (!opt_total.has_value()) {
      *out_code = EFAULT;
      return ZX_OK;
    }
    size_t total = opt_total.value();

    std::optional<SocketAddress> remote_addr;
    // Attempt to load socket address if either name or namelen is set.
    // If only one is set, it'll result in INVALID_ARGS.
    if (msg->msg_namelen != 0 || msg->msg_name != nullptr) {
      zx_status_t status = remote_addr.emplace().LoadSockAddr(
          static_cast<struct sockaddr*>(msg->msg_name), msg->msg_namelen);
      if (status != ZX_OK) {
        return status;
      }
    }

    // TODO(https://fxbug.dev/103740): Avoid allocating into this arena.
    fidl::Arena alloc;
    fit::result cmsg_result =
        ParseControlMessages<fsocket::wire::DatagramSocketSendControlData>(alloc, msghdr_ref);
    if (cmsg_result.is_error()) {
      *out_code = cmsg_result.error_value();
      return ZX_OK;
    }
    const fsocket::wire::DatagramSocketSendControlData& cdata = cmsg_result.value();
    const std::optional local_iface_and_addr =
        [&cdata]() -> std::optional<std::pair<uint64_t, fuchsia_net::wire::Ipv6Address>> {
      if (!cdata.has_network()) {
        return {};
      }
      const fuchsia_posix_socket::wire::NetworkSocketSendControlData& network = cdata.network();
      if (!network.has_ipv6()) {
        return {};
      }
      const fuchsia_posix_socket::wire::Ipv6SendControlData& ipv6 = network.ipv6();
      if (!ipv6.has_pktinfo()) {
        return {};
      }
      const fuchsia_posix_socket::wire::Ipv6PktInfoSendControlData& pktinfo = ipv6.pktinfo();
      return std::make_pair(pktinfo.iface, pktinfo.local_addr);
    }();

    RouteCache::Result result = zxio_datagram_socket().route_cache.Get(
        remote_addr, local_iface_and_addr, socket_err_wait_item(), GetClient());

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
    if (remote_addr.has_value()) {
      serialize_err = remote_addr.value().WithFIDL(
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
    std::optional err = GetErrorWithClient(zxio_datagram_socket().client);
    if (!err.has_value()) {
      return zx::ok(static_cast<int16_t>(0));
    }
    return err.value();
  }
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
    // TODO(https://fxbug.dev/110570) Add tests with msg as nullptr.
    if (msg == nullptr) {
      *out_code = EFAULT;
      return ZX_OK;
    }
    const msghdr& msghdr_ref = *msg;
    fidl::Arena allocator;
    fit::result cmsg_result =
        ParseControlMessages<fsocket::wire::SocketSendControlData>(allocator, msghdr_ref);
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
