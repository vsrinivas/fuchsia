// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_SOCKET_ADDRESS_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_SOCKET_ADDRESS_H_

#include <fidl/fuchsia.net/cpp/wire.h>
#include <fidl/fuchsia.posix.socket.packet/cpp/wire.h>
#include <lib/fidl/cpp/wire/object_view.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <zircon/types.h>

#include <functional>
#include <optional>
#include <type_traits>
#include <variant>

#include <netpacket/packet.h>

// A helper structure to keep a socket address and the variants allocations on the stack.
class SocketAddress {
 public:
  static std::optional<SocketAddress> FromFidl(const fuchsia_net::wire::SocketAddress& from_addr);

  zx_status_t LoadSockAddr(const struct sockaddr* addr, size_t addr_len);

  bool operator==(const SocketAddress& o) const;
  bool operator!=(const SocketAddress& o) const;

  size_t hash() const;

  // Helpers from the reference documentation for std::visit<>, to allow
  // visit-by-overload of the std::variant<> below:
  template <class... Ts>
  struct overloaded : Ts... {
    using Ts::operator()...;
  };

  // explicit deduction guide (not needed as of C++20)
  template <class... Ts>
  overloaded(Ts...) -> overloaded<Ts...>;

  template <typename F>
  std::invoke_result_t<F, fuchsia_net::wire::SocketAddress> WithFIDL(F fn) {
    return fn([this]() -> fuchsia_net::wire::SocketAddress {
      if (storage_.has_value()) {
        return std::visit(
            overloaded{
                [](fuchsia_net::wire::Ipv4SocketAddress& ipv4) {
                  return fuchsia_net::wire::SocketAddress::WithIpv4(
                      fidl::ObjectView<fuchsia_net::wire::Ipv4SocketAddress>::FromExternal(&ipv4));
                },
                [](fuchsia_net::wire::Ipv6SocketAddress& ipv6) {
                  return fuchsia_net::wire::SocketAddress::WithIpv6(
                      fidl::ObjectView<fuchsia_net::wire::Ipv6SocketAddress>::FromExternal(&ipv6));
                },
            },
            storage_.value());
      }
      return {};
    }());
  }

 private:
  std::optional<
      std::variant<fuchsia_net::wire::Ipv4SocketAddress, fuchsia_net::wire::Ipv6SocketAddress>>
      storage_;
};

// Writes |fidl| as a sockaddr in |*addr|. Truncates to |addr_len| if necessary.
//
// Returns the untruncated size of |fidl| as a sockaddr.
socklen_t zxio_fidl_to_sockaddr(const fuchsia_net::wire::SocketAddress& fidl, void* addr,
                                socklen_t addr_len);

// A helper structure to keep a packet info and any members' variants
// allocations on the stack.
class PacketInfo {
 public:
  zx_status_t LoadSockAddr(const sockaddr* addr, size_t addr_len);

  template <typename F>
  std::invoke_result_t<F, fidl::ObjectView<fuchsia_posix_socket_packet::wire::PacketInfo>> WithFIDL(
      F fn) {
    auto packet_info = [this]() -> fuchsia_posix_socket_packet::wire::PacketInfo {
      return {
          .protocol = protocol_,
          .interface_id = interface_id_,
          .addr =
              [this]() {
                if (eui48_storage_.has_value()) {
                  return fuchsia_posix_socket_packet::wire::HardwareAddress::WithEui48(
                      fidl::ObjectView<fuchsia_net::wire::MacAddress>::FromExternal(
                          &eui48_storage_.value()));
                }
                return fuchsia_posix_socket_packet::wire::HardwareAddress::WithNone({});
              }(),
      };
    }();
    return fn(fidl::ObjectView<fuchsia_posix_socket_packet::wire::PacketInfo>::FromExternal(
        &packet_info));
  }

 private:
  decltype(fuchsia_posix_socket_packet::wire::PacketInfo::protocol) protocol_;
  decltype(fuchsia_posix_socket_packet::wire::PacketInfo::interface_id) interface_id_;
  std::optional<fuchsia_net::wire::MacAddress> eui48_storage_;
};

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_SOCKET_ADDRESS_H_
