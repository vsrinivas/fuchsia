// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/zxio/bsdsocket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <zircon/types.h>

#include <algorithm>
#include <bitset>
#include <type_traits>
#include <utility>

#include <fbl/ref_ptr.h>
#include <netpacket/packet.h>

#include "fdio_unistd.h"
#include "sdk/lib/fdio/get_client.h"
#include "zxio.h"

namespace fsocket = fuchsia_posix_socket;
namespace fnet = fuchsia_net;

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

  void wait_begin(uint32_t events, zx_handle_t* handle, zx_signals_t* out_signals) override {
    zxio_wait_begin(&zxio_storage().io, events, handle, out_signals);
  }

  void wait_end(zx_signals_t signals, uint32_t* out_events) override {
    zxio_wait_end(&zxio_storage().io, signals, out_events);
  }

 protected:
  friend class fbl::internal::MakeRefCountedHelper<base_socket>;
  friend class fbl::RefPtr<base_socket>;
};

}  // namespace fdio_internal

fdio_ptr fdio_socket_allocate() { return fbl::MakeRefCounted<fdio_internal::base_socket>(); }
