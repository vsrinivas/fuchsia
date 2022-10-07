// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdk/lib/fdio/socket.h"

#include <lib/fit/result.h>
#include <lib/zxio/bsdsocket.h>
#include <lib/zxio/cpp/create_with_type.h>
#include <lib/zxio/cpp/dgram_cache.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/null.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <zircon/types.h>

#include <algorithm>
#include <bitset>
#include <mutex>
#include <type_traits>
#include <utility>

#include <fbl/ref_ptr.h>
#include <netpacket/packet.h>

#include "fdio_unistd.h"
#include "sdk/lib/fdio/get_client.h"
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
 *          +---------------+
 *          | packet_socket |
 *          |               |
 *          |   Enabled:    |
 *          |  PacketSocket |
 *          |               |
 *          |  Implements:  |
 *          | Overrides for |
 *          |    packet     |
 *          |    sockets    |
 *          |  (AF_PACKET)  |
 *          +---------------+
 *                   ^
 *                   |
 *                   |
 *                   |
 *                   |
 *                   |
 *       +-----------+-----------+      +-----------------+      +-----------------+
 *       |   socket_with_event   |      |  stream_socket  |      | datagram_socket |
 *       |                       |      |                 |      |                 |
 *       |       Enabled:        |      |    Enabled:     |      |    Enabled:     |
 *       |     PacketSocket      |      |   StreamSocket  |      | DatagramSocket  |
 *       |       RawSocket       |      |                 |      |                 |
 *       |    SyncDgramSocket    |      |    Implements:  |      |    Implements:  |
 *       |                       |      |  Overrides for  |      |  Overrides for  |
 *       | Implements: Overrides |      |   SOCK_STREAM   |      |   SOCK_DGRAM    |
 *       |   for sockets using   |      |  sockets using  |      |  sockets using  |
 *       |   FIDL over channel   |      |  a zx::socket   |      |  a zx::socket   |
 *       |    as a data plane    |      |   data plane    |      |   data plane    |
 *       +-----------------------+      +-----------------+      +-----------------+
 *                    ^                          ^                        ^
 *                    |                          |                        |
 *                    |                          |                        |
 *                    +--------------------------+------------------------+
 *                                               |
 *                                               |
 *                                    +----------+---------+
 *                                    |     base_socket    |
 *                                    |                    |
 *                                    |    Enabled: All    |
 *                                    |                    |
 *                                    |    Implements:     |
 *                                    | Overrides for all  |
 *                                    |    socket types    |
 *                                    +--------------------+
 *                                               |
 *                                    +----------+-----------+
 *                                    |         zxio         |
 *                                    |                      |
 *                                    |  Implements: POSIX   |
 *                                    | interface + behavior |
 *                                    |    for generic fds   |
 *                                    +----------------------+
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

  zx_status_t recvmsg(struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    return zxio_recvmsg(&zxio_storage().io, msg, flags, out_actual, out_code);
  }

  zx_status_t sendmsg(const struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    return zxio_sendmsg(&zxio_storage().io, msg, flags, out_actual, out_code);
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

namespace fdio_internal {

struct datagram_socket : public base_socket<DatagramSocket> {
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

 protected:
  friend class fbl::internal::MakeRefCountedHelper<datagram_socket>;
  friend class fbl::RefPtr<datagram_socket>;

  ~datagram_socket() override = default;

 private:
  fidl::WireSyncClient<fsocket::DatagramSocket>& GetClient() override {
    return zxio_datagram_socket(&zxio_storage().io).client;
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

struct stream_socket : public base_socket<StreamSocket> {
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

 private:
  zxio_stream_socket_t& zxio_stream_socket() { return ::zxio_stream_socket(&zxio_storage().io); }
  zxio_stream_socket_state_t& zxio_stream_socket_state() { return zxio_stream_socket().state; }
  std::mutex& zxio_stream_socket_state_lock() { return zxio_stream_socket().state_lock; }

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
