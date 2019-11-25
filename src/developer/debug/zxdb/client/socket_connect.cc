// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/socket_connect.h"

#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/zxdb/common/err.h"

#if defined(__APPLE__)

// NOTE(donosoc): In the MacOS case, getaddrinfo (the approach used for Linux) would result almost
// always in an "Address family not supported by protocol family" error when trying to connect. A
// lot of debugging got nowhere and finally decided to go the inet_pton way.
//
// Ironically, we cannot (easily) use this approach for linux too because turns out that MacOS's
// inet_pton has extended functionality that enables it to support link-local IPv6 addresses that
// specify the interface (which is needed to correctly connect to link-local addresses). In linux is
// a more contrived dance that requires to iterate over all the interfaces, so it's simpler to go
// the normal getaddrinfo route.
//
// Some background info that lead me to discard getaddrinfo altogether:
//
// https://blog.powerdns.com/2014/05/21/a-surprising-discovery-on-converting-ipv6-addresses-we-no-longer-prefer-getaddrinfo/

#include <arpa/inet.h>
#include <fcntl.h>

#include <variant>

namespace zxdb {

namespace {

using SockAddrVariant = std::variant<sockaddr_in, sockaddr_in6>;

Err ResolveTargetAddress(const std::string& host, uint16_t port, SockAddrVariant* out) {
  int res;
  // First try IPv6. Result of 0 means that the string is not IPv6.
  struct sockaddr_in6 addr6 = {};  // zero-out.
  res = inet_pton(AF_INET6, host.c_str(), &addr6.sin6_addr);
  if (res == 1) {
    // Successfully found IPv6 address.
    addr6.sin6_family = AF_INET6;
    addr6.sin6_port = htons(port);
    *out = std::move(addr6);
    return Err();
  }

  DEBUG_LOG(RemoteAPI) << "Could not resolve IPv6: " << strerror(errno) << " (res: " << res << ").";

  // We now try IPv4.
  struct sockaddr_in addr4 = {};  // zero-out.
  res = inet_pton(AF_INET, host.c_str(), &addr4.sin_addr);
  if (res == 1) {
    // Successfully found IPv4 address.
    addr4.sin_family = AF_INET;
    addr4.sin_port = htons(port);
    *out = std::move(addr4);
    return Err();
  }

  return Err("Address %s is not a valid IPv4 or IPv6 address.", host.c_str());
}

}  // namespace

Err ConnectToHost(const std::string& host, uint16_t port, fbl::unique_fd* socket_out) {
  SockAddrVariant addr_variant;
  Err err = ResolveTargetAddress(host, port, &addr_variant);
  if (err.has_error())
    return err;

  fbl::unique_fd res_socket;

  // Is it IPv6?
  if (std::holds_alternative<sockaddr_in6>(addr_variant)) {
    res_socket.reset(socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP));
    if (!res_socket.is_valid())
      return Err("Could not create socket: %s.", strerror(errno));

    sockaddr_in6& addr6 = std::get<sockaddr_in6>(addr_variant);
    if (connect(res_socket.get(), reinterpret_cast<sockaddr*>(&addr6), sizeof(sockaddr_in6))) {
      return Err("Could not connect to socket: %s.", strerror(errno));
    }
  }

  // Fallback to IPv4.
  if (std::holds_alternative<sockaddr_in>(addr_variant)) {
    res_socket.reset(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!res_socket.is_valid())
      return Err("Could not create socket: %s.", strerror(errno));

    sockaddr_in& addr4 = std::get<sockaddr_in>(addr_variant);
    if (connect(res_socket.get(), reinterpret_cast<sockaddr*>(&addr4), sizeof(sockaddr_in))) {
      return Err("Could not connect to socket: %s.", strerror(errno));
    }
  }

  // By default sockets are blocking which we don't want.
  if (fcntl(res_socket.get(), F_SETFL, O_NONBLOCK) < 0)
    return Err("Could not make nonblocking socket.");

  *socket_out = std::move(res_socket);
  return Err();
}

}  // namespace zxdb

#elif defined(__linux__)

#include <fcntl.h>
#include <netdb.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Tries to resolve the host/port. On success populates *addr and returns Err().
Err ResolveAddress(const std::string& host, uint16_t port, addrinfo* addr) {
  std::string port_str = fxl::StringPrintf("%u", port);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  struct addrinfo* addrs = nullptr;
  int addr_err = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &addrs);
  if (addr_err != 0) {
    return Err("Failed to resolve %s: %s.", host.c_str(), gai_strerror(addr_err));
  }

  struct addrinfo* p;
  for (p = addrs; p != nullptr; p = p->ai_next) {
    char buf[1024];
    getnameinfo(p->ai_addr, p->ai_addrlen, buf, sizeof(buf), nullptr, 0, NI_NUMERICHOST);
  }

  *addr = *addrs;
  freeaddrinfo(addrs);
  return Err();
}

}  // namespace

Err ConnectToHost(const std::string& host, uint16_t port, fbl::unique_fd* socket_out) {
  addrinfo addr;
  Err err = ResolveAddress(host, port, &addr);
  if (err.has_error())
    return err;

  fbl::unique_fd res_socket;

  res_socket.reset(socket(addr.ai_family, SOCK_STREAM, IPPROTO_TCP));
  if (!res_socket.is_valid())
    return Err("Could not create socket: %s.", strerror(errno));

  if (connect(res_socket.get(), addr.ai_addr, addr.ai_addrlen))
    return Err("Failed to connect socket: %s.", strerror(errno));

  // By default sockets are blocking which we don't want.
  if (fcntl(res_socket.get(), F_SETFL, O_NONBLOCK) < 0)
    return Err("Could not make nonblocking socket: %s.", strerror(errno));

  *socket_out = std::move(res_socket);
  return Err();
}

}  // namespace zxdb

#else
#error Unsupported OS
#endif
